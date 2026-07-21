#include "core2_sleep.h"

#include "esp_log.h"
#include "bsp/m5stack_core_2.h"
#include "core2_power.h"
#include "ledstrip_fx.h"
#include "haptics.h"

static const char *TAG = "core2_sleep";

// 实机定案默认值(来源:倾斜迷宫 2026-07-01 实机调通,CLAUDE.md §20.2/§20.6)
#define DEF_NAP_AFTER_MS   12000
#define DEF_DEEP_AFTER_MS  60000
#define DEF_AWAKE_BRIGHT   60
#define DEF_NAP_BRIGHT     10
#define DEF_FRAME_MS       16
#define DEF_DEEP_POLL_MS   120

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

void core2_sleep_init(core2_sleep_t *s, const core2_sleep_cfg_t *cfg)
{
    core2_sleep_cfg_t c = cfg ? *cfg : CORE2_SLEEP_CFG_DEFAULT;
    if (c.nap_after_ms  <= 0) c.nap_after_ms  = DEF_NAP_AFTER_MS;
    if (c.deep_after_ms <= 0) c.deep_after_ms = DEF_DEEP_AFTER_MS;
    if (c.awake_brightness <= 0) c.awake_brightness = DEF_AWAKE_BRIGHT;
    if (c.nap_brightness   <= 0) c.nap_brightness   = DEF_NAP_BRIGHT;
    if (c.frame_ms      <= 0) c.frame_ms      = DEF_FRAME_MS;
    if (c.deep_poll_ms  <= 0) c.deep_poll_ms  = DEF_DEEP_POLL_MS;

    s->cfg    = c;
    s->stage  = CORE2_SLEEP_AWAKE;
    s->frames = 0;
    motion_detect_init(&s->md, c.wake_thresh, c.wake_frames);
}

int core2_sleep_feed(core2_sleep_t *s, const float accel_g[3], bool nap_eligible)
{
    motion_detect_feed(&s->md, accel_g);

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
            if (motion_detect_tick_wake(&s->md)) { do_wake(s); break; }
            s->frames++;   // 单帧噪声尖峰不打断计时(tick_wake 已去抖)
            if (s->frames > s->cfg.deep_after_ms / s->cfg.frame_ms) enter_deep(s);
            break;

        case CORE2_SLEEP_DEEP:
            if (motion_detect_tick_wake(&s->md)) do_wake(s);
            break;
    }
    return (s->stage == CORE2_SLEEP_DEEP) ? s->cfg.deep_poll_ms : s->cfg.frame_ms;
}

core2_sleep_stage_t core2_sleep_stage(const core2_sleep_t *s) { return s->stage; }

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
