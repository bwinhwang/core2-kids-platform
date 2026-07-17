#include "power_lab_ctl.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"

#include "bsp/m5stack_core_2.h"   // bsp_display_brightness_set(见 apps/tilt_maze/main/game_state.c
                                  // 与 components/core2_sleep 同样的用法先例:此调用是背光电源
                                  // 控制而非 LVGL 部件操作,model 层直接调不违反"不碰 LVGL"边界)

#include "audio_fx.h"
#include "core2_power.h"
#include "data_log.h"
#include "haptics.h"
#include "ledstrip_fx.h"

static const char *TAG = "pl_ctl";

// 平台常量(CLAUDE.md §3.2:Bottom2 电池 500mAh,唯一供电来源)
#define PL_BATT_CAPACITY_MAH 500.0f

#define PL_TELEM_POLL_MS 1000   // P1 遥测轮询节拍(power_monitor 的 AXP192 ADC 本身就是这个量级)
#define PL_CHART_AVG_WIN 4      // 电流 chart 滑动平均窗口(power_monitor README:小负载易被噪声淹)

#define PL_NAP_DRILL_MS  6000   // 演练 NAP 停留时长
#define PL_DEEP_DRILL_MS 20000  // 演练 DEEP 停留时长(10~30s 区间,足够看出电流台阶又不用久等)
#define PL_DRILL_SAMPLE_MS  1000
#define PL_DRILL_SAMPLE_CAP 32   // 20s@1Hz=20 个采样点,留余量到 32

static const int    s_backlight_levels[] = { 0, 10, 60, 100 };
static const int    s_led_levels[]       = { 0, 48, 255 };
#define PL_BACKLIGHT_LEVELS_N ((int)(sizeof(s_backlight_levels) / sizeof(s_backlight_levels[0])))
#define PL_LED_LEVELS_N       ((int)(sizeof(s_led_levels) / sizeof(s_led_levels[0])))

// 演练采样缓冲(内存环形数组,不需要 UI 刷新——见头文件注释与 CLAUDE.md 任务说明)
static float   s_drill_samples[PL_DRILL_SAMPLE_CAP];
static int     s_drill_sample_count;

// ── 内部小工具 ──────────────────────────────────────────────────────────────

static void apply_backlight(int idx)
{
    int pct = s_backlight_levels[idx];
    if (pct == 0) {
        // 真熄屏演示:brightness_set(0) 不断电(仍 ~2.95V),必须叠加断 DCDC3 使能位
        // (CLAUDE.md §7 坑 3)。触屏本身走独立的 FT6336U I2C,不受 DCDC3 影响,盲点同一
        // 按钮位置仍能翻到下一档——已知交互特性,记入 README。
        bsp_display_brightness_set(0);
        core2_power_backlight(false);
    } else {
        core2_power_backlight(true);   // 先恢复 DCDC3 使能,再调亮度,避免"背光开着但没使能"的中间态
        bsp_display_brightness_set(pct);
    }
}

static void apply_led(int idx)
{
    ledstrip_fx_set_base(LED_BASE_AMBIENT);   // 保证处于常亮态而非 IDLE 呼吸/OFF,亮度台阶才稳定可读
    ledstrip_fx_set_max_brightness((uint8_t)s_led_levels[idx]);
}

static void apply_cpu_mode(pl_ctl_t *c)
{
    if (!c->cpu_pm_available) return;

    esp_pm_config_t cfg = { 0 };
    if (c->cpu_mode == PL_CPU_FIXED_240) {
        cfg.max_freq_mhz = 240;
        cfg.min_freq_mhz = 240;
    } else {
        cfg.max_freq_mhz = 240;
        cfg.min_freq_mhz = 80;
    }
    cfg.light_sleep_enable = false;   // 见 README「CPU 锁频查证结论」:刻意不开自动轻睡眠

    esp_err_t err = esp_pm_configure(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_pm_configure 失败(%s),CPU 挡位可能未生效", esp_err_to_name(err));
    }
}

// ── 初始化 ──────────────────────────────────────────────────────────────────

void pl_ctl_init(pl_ctl_t *c, core2_sleep_t *sleep)
{
    memset(c, 0, sizeof(*c));
    c->sleep = sleep;
    strncpy(c->rec_status, "未录制", sizeof(c->rec_status) - 1);

    esp_err_t perr = power_monitor_init();
    if (perr != ESP_OK) {
        ESP_LOGE(TAG, "power_monitor_init 失败(%s):遥测面板将显示未就绪", esp_err_to_name(perr));
    }

    // 探测 esp_pm_configure 是否可用(CONFIG_PM_ENABLE 未开时返回 ESP_ERR_NOT_SUPPORTED)。
    // 这次调用同时把 CPU 锁到 240MHz 恒定——正好是下面矩阵基线要的 PL_CPU_FIXED_240 起点,
    // 探测与置基线一步做完,不是两次分离的动作。
    esp_pm_config_t probe = { .max_freq_mhz = 240, .min_freq_mhz = 240, .light_sleep_enable = false };
    esp_err_t pmerr = esp_pm_configure(&probe);
    c->cpu_pm_available = (pmerr == ESP_OK);
    if (!c->cpu_pm_available) {
        ESP_LOGW(TAG, "esp_pm_configure 不可用(%s):CPU 锁频矩阵行降级为禁用态,见 README",
                 esp_err_to_name(pmerr));
    }
    c->cpu_mode = PL_CPU_FIXED_240;   // 与上面探测调用的实际状态一致,无需重复 apply

    // 负载矩阵基线:全部由本函数显式设一遍,不依赖 core2_board_init 的默认值(cfg.brightness_pct=70
    // 不落在四档之一;EXTEN/led 也统一收敛到一个已知起点,方便"开关前后台阶清晰"的实机验收)。
    c->backlight_idx = 2;   // 60%,可读性优先又不刺眼
    apply_backlight(c->backlight_idx);

    c->led_idx = 0;   // 0(灯带逻辑上仍 AMBIENT,只是亮度缩到 0,与"全部负载先关"的矩阵基线一致)
    apply_led(c->led_idx);

    c->exten_on = false;   // 基线关 M-Bus 5V:顺带演示"灯带 refresh 返回 OK 但不亮"这个平台坑
    core2_power_bus_5v(false);

    ESP_LOGI(TAG, "负载矩阵基线就绪:背光60%%/灯带0/EXTEN关/CPU定频,cpu_pm_available=%d",
             c->cpu_pm_available);
}

// ── 主循环 tick ─────────────────────────────────────────────────────────────

static void telemetry_tick(pl_ctl_t *c, int64_t now_ms)
{
    if (now_ms - c->last_telem_ms < PL_TELEM_POLL_MS) return;
    c->last_telem_ms = now_ms;

    power_telemetry_t t;
    if (power_monitor_read(&t) != ESP_OK) {
        c->telem_valid = false;
        return;
    }
    c->telem = t;
    c->telem_valid = true;

    // chart 取值:VBUS 在位时看 VBUS 电流才有意义(插着 USB 电池电流≈0,power_monitor README
    // 已记录此坑);否则看电池净电流(充电为正、放电为负)。
    float raw;
    if (t.vbus_present) {
        raw = (float)t.vbus_ma;
        c->chart_from_vbus = true;
    } else {
        raw = (float)(t.batt_charge_ma - t.batt_discharge_ma);
        c->chart_from_vbus = false;
    }

    // 简单滑动平均(定长历史 + 累计和,避免每次都重扫整个窗口)
    static float hist[PL_CHART_AVG_WIN];
    static int   hist_n = 0, hist_i = 0;
    static float hist_sum = 0.0f;
    if (hist_n < PL_CHART_AVG_WIN) {
        hist[hist_i] = raw;
        hist_sum += raw;
        hist_n++;
    } else {
        hist_sum -= hist[hist_i];
        hist[hist_i] = raw;
        hist_sum += raw;
    }
    hist_i = (hist_i + 1) % PL_CHART_AVG_WIN;
    c->chart_ma_smoothed = hist_sum / (float)hist_n;

    // SPIFFS 离线录制中:每次成功读到遥测就顺带写一行(与 rec_start 声明的列顺序一一对应,
    // 见 rec_tick 里的 cols_csv 字符串)。写在 telemetry_tick 里而不是单独的 1Hz 定时器,
    // 复用同一次 power_monitor_read 结果,不重复读 AXP192。
    if (c->rec_active) {
        data_log_rec_row("%d,%d,%d,%d,%d,%d,%d",
                          t.batt_mv, t.batt_charge_ma, t.batt_discharge_ma,
                          t.vbus_present ? 1 : 0, t.vbus_mv, t.vbus_ma, t.charging ? 1 : 0);
    }
}

// 真正应用一次演练(force_stage 是阻塞调用,内部顺序见 core2_sleep.h/README,不许在这里
// 之外的地方重拼)。只由 drill_tick 在主循环上下文调用,不直接暴露给 UI(见头文件注释)。
static void apply_drill_start(pl_ctl_t *c, pl_drill_stage_t stage, int64_t now_ms)
{
    s_drill_sample_count    = 0;
    c->last_drill_sample_ms = now_ms;
    c->drill_stage          = stage;
    c->drill_start_ms       = now_ms;
    c->drill_end_ms         = now_ms + (stage == PL_DRILL_NAP ? PL_NAP_DRILL_MS : PL_DEEP_DRILL_MS);

    core2_sleep_force_stage(c->sleep, stage == PL_DRILL_NAP ? CORE2_SLEEP_NAP : CORE2_SLEEP_DEEP);
}

static void drill_tick(pl_ctl_t *c, int64_t now_ms)
{
    if (c->drill_pending != PL_DRILL_IDLE && c->drill_stage == PL_DRILL_IDLE) {
        pl_drill_stage_t want = c->drill_pending;
        c->drill_pending = PL_DRILL_IDLE;
        apply_drill_start(c, want, now_ms);
        return;   // 这一帧只管起演练,采样从下一帧开始,逻辑更清楚
    }

    if (c->drill_stage == PL_DRILL_IDLE) return;

    if (now_ms - c->last_drill_sample_ms >= PL_DRILL_SAMPLE_MS) {
        c->last_drill_sample_ms = now_ms;
        power_telemetry_t t;
        if (power_monitor_read(&t) == ESP_OK && s_drill_sample_count < PL_DRILL_SAMPLE_CAP) {
            // 演练场景默认已拔线自测放电(即便插着 USB,也记录放电电流,不做特判——
            // 演练的目的是看"进入该阶段后电流是否下降",VBUS 在位与否由用户自己掌握)。
            s_drill_samples[s_drill_sample_count++] = (float)t.batt_discharge_ma;
        }
    }

    if (now_ms >= c->drill_end_ms) {
        pl_drill_stage_t finished = c->drill_stage;
        core2_sleep_force_stage(c->sleep, CORE2_SLEEP_AWAKE);   // 依次恢复 5V→背光→灯带→(可选)轻震

        float sum = 0.0f;
        for (int i = 0; i < s_drill_sample_count; i++) sum += s_drill_samples[i];
        c->drill_avg_ma      = (s_drill_sample_count > 0) ? (sum / (float)s_drill_sample_count) : 0.0f;
        c->drill_sample_n    = s_drill_sample_count;
        c->drill_duration_ms = (int)(now_ms - c->drill_start_ms);
        c->drill_last_stage  = finished;
        c->drill_have_result = true;
        c->drill_stage       = PL_DRILL_IDLE;

        // 演练结束后负载矩阵的物理状态已被 core2_sleep 的唤醒顺序恢复到"清醒"默认值
        // (亮度/灯带回到它自己的 play 档),与本文件的 backlight_idx/led_idx 记录脱节——
        // 演练本来就是"临时接管"性质,重新贴回矩阵此刻记的挡位,保持 UI 与实际一致。
        apply_backlight(c->backlight_idx);
        apply_led(c->led_idx);
        core2_power_bus_5v(c->exten_on);

        ESP_LOGI(TAG, "演练结束(%s):均值 %.1fmA,%d 个采样点,时长%dms",
                 finished == PL_DRILL_NAP ? "NAP" : "DEEP", c->drill_avg_ma, c->drill_sample_n,
                 c->drill_duration_ms);
    }
}

static void rec_tick(pl_ctl_t *c)
{
    // 两段式请求 —— 阻塞的 SPIFFS 挂载/格式化/文件 IO 只在这里(app_task 主循环上下文,
    // 不持 bsp_display_lock)执行一次,详见 components/data_log/README.md「设计取舍」。
    if (c->rec_pending_start) {
        c->rec_pending_start = false;
        esp_err_t err = data_log_rec_start("power_lab",
            "batt_mv,batt_charge_ma,batt_discharge_ma,vbus_present,vbus_mv,vbus_ma,charging");
        if (err == ESP_OK) {
            c->rec_active = true;
            strncpy(c->rec_status, "录制中", sizeof(c->rec_status) - 1);
        } else {
            c->rec_active = false;
            snprintf(c->rec_status, sizeof(c->rec_status), "启动失败(%s)", esp_err_to_name(err));
        }
    }

    if (c->rec_pending_dump) {
        c->rec_pending_dump = false;
        esp_err_t err = data_log_rec_dump();
        c->rec_active = false;   // rec_dump 内部会先自动 rec_stop
        if (err == ESP_OK) strncpy(c->rec_status, "导出完成(见串口)", sizeof(c->rec_status) - 1);
        else                snprintf(c->rec_status, sizeof(c->rec_status), "导出失败(%s)", esp_err_to_name(err));
    }
}

void pl_ctl_tick(pl_ctl_t *c, int64_t now_ms)
{
    telemetry_tick(c, now_ms);
    drill_tick(c, now_ms);
    rec_tick(c);
}

// ── 负载开关矩阵动作 ────────────────────────────────────────────────────────

void pl_ctl_cycle_backlight(pl_ctl_t *c)
{
    c->backlight_idx = (c->backlight_idx + 1) % PL_BACKLIGHT_LEVELS_N;
    apply_backlight(c->backlight_idx);
}

void pl_ctl_cycle_led(pl_ctl_t *c)
{
    c->led_idx = (c->led_idx + 1) % PL_LED_LEVELS_N;
    apply_led(c->led_idx);
}

void pl_ctl_set_exten(pl_ctl_t *c, bool on)
{
    c->exten_on = on;
    core2_power_bus_5v(on);
}

void pl_ctl_cycle_cpu(pl_ctl_t *c)
{
    if (!c->cpu_pm_available) return;
    c->cpu_mode = (c->cpu_mode == PL_CPU_FIXED_240) ? PL_CPU_AUTO_DFS : PL_CPU_FIXED_240;
    apply_cpu_mode(c);
}

void pl_ctl_test_audio(void)
{
    // audio_fx 单段合成上限 ~400ms(见 audio_fx.h),用一个稳定音高的持续音当"喇叭负载"演示。
    static const audio_note_t notes[] = { { 880, 350, 80 } };
    audio_fx_play_notes(notes, 1);
}

void pl_ctl_test_haptic(void)
{
    haptics_play(HAPTIC_BUMP_HARD);   // 幅度最大的内置模式,负载演示优先"看得出来"而非"轻柔"
}

int pl_ctl_backlight_pct(const pl_ctl_t *c) { return s_backlight_levels[c->backlight_idx]; }
int pl_ctl_led_level(const pl_ctl_t *c)     { return s_led_levels[c->led_idx]; }

// ── 休眠演练 ────────────────────────────────────────────────────────────────

void pl_ctl_request_drill(pl_ctl_t *c, pl_drill_stage_t stage)
{
    if (stage != PL_DRILL_NAP && stage != PL_DRILL_DEEP) return;
    if (c->drill_stage != PL_DRILL_IDLE || c->drill_pending != PL_DRILL_IDLE) return;   // 已在忙,忽略
    c->drill_pending = stage;
}

// ── 续航估算 ────────────────────────────────────────────────────────────────

float pl_ctl_endurance_hours(const pl_ctl_t *c)
{
    if (!c->telem_valid) return -1.0f;
    if (c->telem.charging) return -1.0f;              // 充电中,"剩余小时数"没有意义
    if (c->telem.batt_discharge_ma <= 1) return -1.0f;  // 电流太小/为 0,外推没有意义(也避免除零)
    return PL_BATT_CAPACITY_MAH / (float)c->telem.batt_discharge_ma;
}

// ── SPIFFS 离线录制请求 ──────────────────────────────────────────────────────

void pl_ctl_request_rec_start(pl_ctl_t *c)
{
    if (c->rec_active) return;
    c->rec_pending_start = true;
    strncpy(c->rec_status, "初始化存储中…", sizeof(c->rec_status) - 1);
}

void pl_ctl_request_rec_stop(pl_ctl_t *c)
{
    if (!c->rec_active) return;
    data_log_rec_stop();   // flush+close 很快,不需要走两段式
    c->rec_active = false;
    strncpy(c->rec_status, "未录制", sizeof(c->rec_status) - 1);
}

void pl_ctl_request_rec_dump(pl_ctl_t *c)
{
    c->rec_pending_dump = true;
    strncpy(c->rec_status, "导出中…", sizeof(c->rec_status) - 1);
}
