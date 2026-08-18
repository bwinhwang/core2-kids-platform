#include "core2_sleep.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/m5stack_core_2.h"
#include "core2_power.h"
#include "power_monitor.h"
#include "ledstrip_fx.h"
#include "audio_fx.h"
#include "haptics.h"

static const char *TAG = "core2_sleep";

// 实机定案默认值(来源:倾斜迷宫 2026-07-01 实机调通,CLAUDE.md §20.2/§20.6)
#define DEF_NAP_AFTER_MS   12000
#define DEF_DEEP_AFTER_MS  60000
#define DEF_AWAKE_BRIGHT   60
#define DEF_NAP_BRIGHT     10
#define DEF_FRAME_MS       16
#define DEF_DEEP_POLL_MS   120
#define DEF_DEEP_SHUT_MS   600000   // DEEP 满 10min 自动关机(总计放下 ~11min 后断电)
#define DEF_CRIT_BAT_MV    3300     // 低于此电压关机;AXP192 自身保护在 3.0V,别等到那

#define PROBE_EVERY_MS     10000    // 读电量的节拍(I2C 几字节,10s 一次的开销可忽略)
#define LOW_HITS_TO_SHUT   2        // 连续两次读到低电才关机(防单次坏读误关)
#define LATE_LOG_EVERY_MS  5000     // 帧逾期丢帧的日志节流窗口

static void change_stage(core2_sleep_t *s, core2_sleep_stage_t to)
{
    core2_sleep_stage_t from = s->stage;
    s->stage  = to;
    s->frames = 0;
    motion_detect_reset(&s->md);
    if (s->cfg.on_stage_change) s->cfg.on_stage_change(from, to);
}

static void enter_nap(core2_sleep_t *s)
{
    bsp_display_brightness_set(s->cfg.nap_brightness);
    if (s->cfg.manage_leds) ledstrip_fx_set_base(LED_BASE_IDLE);
    ESP_LOGI(TAG, "NAP:打盹降亮(动一下唤醒)");
    change_stage(s, CORE2_SLEEP_NAP);
}

// 深度省电:每步顺序都是实机结论,勿重排(brightness 0% 不熄屏,必须断 DCDC3)
static void enter_deep(core2_sleep_t *s)
{
    bsp_display_brightness_set(0);                        // 先把 DCDC3 电压降到最低
    core2_power_backlight(false);                          // 再断 DCDC3 使能 → 背光真全黑
    if (s->cfg.manage_leds)    ledstrip_fx_set_base(LED_BASE_OFF);
    if (s->cfg.manage_bus_5v)  core2_power_bus_5v(false);  // 切 5V(断灯带 + SY7088 静态电流)
    ESP_LOGI(TAG, "DEEP:关屏关灯带深度省电,轮询降到 %dms", s->cfg.deep_poll_ms);
    change_stage(s, CORE2_SLEEP_DEEP);
}

static void do_wake(core2_sleep_t *s)
{
    if (s->cfg.manage_bus_5v) core2_power_bus_5v(true);   // 先恢复 5V(若来自深度省电)
    core2_power_backlight(true);                           // 重启 DCDC3
    bsp_display_brightness_set(s->cfg.awake_brightness);
    if (s->cfg.manage_leds)  ledstrip_fx_set_base(LED_BASE_AMBIENT);
    if (s->cfg.wake_haptic)  haptics_play(HAPTIC_WAKE);
    ESP_LOGI(TAG, "唤醒 → AWAKE");
    change_stage(s, CORE2_SLEEP_AWAKE);
}

// 真关机。此前所有"省电"都只是关外设,主控一直满速跑 —— 久置最后只有断电才能止住耗电。
static void do_shutdown(core2_sleep_t *s, const char *why, const power_status_t *st)
{
    ESP_LOGW(TAG, "自动关机(%s):电池 %dmV(~%d%%)。按电源键可重新开机", why,
             st->bat_mv, st->pct);
    // 屏和灯带此刻多半已灭,"要关机了"只能靠震动+声音传达(和平板一个直觉)。
    // ⚠️ 震动是可靠的那条(LDO3 独立供电);NS4168 功放的 VDD 是否也吃已被切掉的 M-Bus 5V
    // 手册没写明 → DEEP 里这声可能是哑的,别让它成为唯一线索(待实机确认)。
    haptics_play(HAPTIC_BUMP_MED);
    audio_fx_play(SND_BUMP_LIGHT);
    vTaskDelay(pdMS_TO_TICKS(400));        // 让提示播完再断电,否则只听到半声
    core2_power_shutdown();                 // AXP192 0x32 bit7:正常不返回
    ESP_LOGE(TAG, "关机写寄存器返回了(I2C 失败?),继续深度省电");
}

// 定期读电量:打日志(§5.1「先量,别猜」的那条数)+ 判低电关机 + 判 DEEP 久置关机。
static void power_check(core2_sleep_t *s)
{
    power_status_t st;
    if (power_monitor_read(&st) != ESP_OK || st.bat_mv <= 0) return;  // 读不到就别据此决策

    if (s->stage == CORE2_SLEEP_DEEP) {
        // DEEP 里这条日志是拿"还在耗多少"的唯一直接证据(拔 USB 才是真读数,见 README)
        ESP_LOGI(TAG, "DEEP %ds:电池 %dmV(~%d%%)%+dmA%s",
                 s->frames * s->cfg.deep_poll_ms / 1000, st.bat_mv, st.pct, st.bat_ma,
                 st.usb ? " [USB 在位,不关机]" : "");
    }

    if (st.usb) {           // 插着 USB:在充电/在开发,关机既没意义又碍事
        s->low_hits = 0;
        return;
    }

    if (s->cfg.crit_bat_mv > 0 && st.bat_mv < s->cfg.crit_bat_mv) {
        if (++s->low_hits >= LOW_HITS_TO_SHUT) do_shutdown(s, "电量耗尽", &st);
        return;
    }
    s->low_hits = 0;

    if (s->stage == CORE2_SLEEP_DEEP && s->cfg.deep_shutdown_ms > 0 &&
        s->frames * s->cfg.deep_poll_ms >= s->cfg.deep_shutdown_ms) {
        do_shutdown(s, "久置无人玩", &st);
    }
}

void core2_sleep_init(core2_sleep_t *s, const core2_sleep_cfg_t *cfg)
{
    core2_sleep_cfg_t c = cfg ? *cfg : CORE2_SLEEP_CFG_DEFAULT;
    if (c.nap_after_ms  <= 0) c.nap_after_ms  = DEF_NAP_AFTER_MS;
    if (c.deep_after_ms <= 0) c.deep_after_ms = DEF_DEEP_AFTER_MS;
    if (c.awake_brightness <= 0) c.awake_brightness = DEF_AWAKE_BRIGHT;
    if (c.nap_brightness   <= 0) c.nap_brightness   = DEF_NAP_BRIGHT;
    if (c.frame_ms      <= 0) c.frame_ms      = DEF_FRAME_MS;
    if (c.deep_poll_ms  <= 0) c.deep_poll_ms  = DEF_DEEP_POLL_MS;
    // 这两个 0=默认、负数=关闭(见头文件);负数在这里统一归一成 0 表示"关"
    if (c.deep_shutdown_ms == 0) c.deep_shutdown_ms = DEF_DEEP_SHUT_MS;
    else if (c.deep_shutdown_ms < 0) c.deep_shutdown_ms = 0;
    if (c.crit_bat_mv == 0) c.crit_bat_mv = DEF_CRIT_BAT_MV;
    else if (c.crit_bat_mv < 0) c.crit_bat_mv = 0;

    s->cfg      = c;
    s->stage    = CORE2_SLEEP_AWAKE;
    s->frames   = 0;
    s->probe_ms = 0;
    s->low_hits = 0;
    motion_detect_init(&s->md, c.wake_thresh, c.wake_frames);
    ESP_LOGI(TAG, "省电编排:打盹 %ds → 深度 %ds → %s;低电关机 %s",
             c.nap_after_ms / 1000, c.deep_after_ms / 1000,
             c.deep_shutdown_ms ? "自动关机" : "永不关机(已禁用)",
             c.crit_bat_mv ? "开" : "关");
}

int core2_sleep_feed(core2_sleep_t *s, const float accel_g[3], bool nap_eligible)
{
    motion_detect_feed(&s->md, accel_g);
    s->frames++;                     // 各阶段共用的阶段内帧计数(change_stage 清零)
    s->probe_ms += (s->stage == CORE2_SLEEP_DEEP) ? s->cfg.deep_poll_ms : s->cfg.frame_ms;

    switch (s->stage) {
        case CORE2_SLEEP_AWAKE:
            if (nap_eligible) {
                if (motion_detect_tick_still(&s->md) > s->cfg.nap_after_ms / s->cfg.frame_ms) {
                    enter_nap(s);
                }
            } else {
                motion_detect_reset(&s->md);   // 非可打盹状态:静止计时清零
            }
            break;

        case CORE2_SLEEP_NAP:
            // 单帧噪声尖峰不打断计时(tick_wake 已去抖)
            if (motion_detect_tick_wake(&s->md)) { do_wake(s); break; }
            if (s->frames > s->cfg.deep_after_ms / s->cfg.frame_ms) enter_deep(s);
            break;

        case CORE2_SLEEP_DEEP:
            if (motion_detect_tick_wake(&s->md)) do_wake(s);
            break;
    }

    if (s->probe_ms >= PROBE_EVERY_MS) {
        s->probe_ms = 0;
        power_check(s);   // 低电关机在任何阶段都查(玩着玩着耗干同样要防深放电)
    }
    return (s->stage == CORE2_SLEEP_DEEP) ? s->cfg.deep_poll_ms : s->cfg.frame_ms;
}

core2_sleep_stage_t core2_sleep_stage(const core2_sleep_t *s) { return s->stage; }

bool core2_sleep_pace(TickType_t *last, int delay_ms)
{
    if (xTaskDelayUntil(last, pdMS_TO_TICKS(delay_ms)) != pdFALSE) return true;

    // 逾期(本帧干的活吃掉了整个周期):迟到的时间直接丢掉,不许留在 last 里攒成欠债
    *last = xTaskGetTickCount();

    static int        late_n;
    static TickType_t late_log;
    late_n++;
    if (*last - late_log >= pdMS_TO_TICKS(LATE_LOG_EVERY_MS)) {
        ESP_LOGW(TAG, "帧超时丢帧 %d 次/%d 秒(渲染跟不上 %dms 节拍)",
                 late_n, LATE_LOG_EVERY_MS / 1000, delay_ms);
        late_n   = 0;
        late_log = *last;
    }
    return false;
}

float core2_sleep_motion(const core2_sleep_t *s) { return s->md.motion; }

void core2_sleep_wake(core2_sleep_t *s)
{
    if (s->stage != CORE2_SLEEP_AWAKE) do_wake(s);
}

void core2_sleep_kick(core2_sleep_t *s)
{
    motion_detect_reset(&s->md);
}

void core2_sleep_set_awake_brightness(core2_sleep_t *s, int pct)
{
    if (pct < 5)   pct = 5;
    if (pct > 100) pct = 100;
    s->cfg.awake_brightness = pct;
    if (s->stage == CORE2_SLEEP_AWAKE) bsp_display_brightness_set(pct);
}

void core2_sleep_force_stage(core2_sleep_t *s, core2_sleep_stage_t stage)
{
    if (stage == s->stage) return;  // 已在目标阶段,空操作
    switch (stage) {
        case CORE2_SLEEP_AWAKE: do_wake(s);   break;
        case CORE2_SLEEP_NAP:   enter_nap(s); break;
        case CORE2_SLEEP_DEEP:  enter_deep(s); break;
    }
}
