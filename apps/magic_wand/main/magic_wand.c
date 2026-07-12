// magic_wand v2.1「魔法萤火虫」(Plan B:在场+手势)—— 主任务:在场/手势轮询 →
// 在场信号 EMA/迟滞/档位(本文件)→ 盘旋推进/翻滚派发(firefly.c 内)→ 状态机 →
// 省电/容错(SPEC.md §3/§4/§7,30Hz)。
//
// v2 的"光标模式连续跟手"已实机否决(157s 实测占空比 ~50%、31 次中断,SPEC §0),
// 本文件是 v2.1 的重写:不再有连续坐标,改为"在场信号(0xB0 亮度)驱动贴玻璃盘旋
// 强度 + 九手势离散事件驱动方向翻滚"。
//
// 状态机(SPEC §3):
//   ST_NO_UNIT  ──(Gesture 接管成功,默认手势模式)──► ST_SEEK
//     ▲(拔线/连续读失败 ERR_STREAK_LOST)──────────────────┘
//   ST_SEEK     萤火虫停在家花、慢眨眼 ──(在场信号 ON,迟滞后)──► ST_DANCE(+ 可能「你好」)
//   ST_DANCE    贴玻璃盘旋(强度三档随在场信号档位)+ 手势→方向翻滚 + 近距音阶
//               ──(在场信号 OFF 且保持 PRES_HOLD_MS 耗尽)──► 挥手再见 → ST_GOING_HOME
//               保持期内(OFF 但未耗尽)舞照跳,强制降到最低档,不 kick、不算离场。
//   ST_GOING_HOME 回家动画(HOME_FLY_MS)──(自然播完)──► ST_SEEK
//               ──(途中在场信号重新 ON)──► 掐动画,立即回 ST_DANCE
//   省电三态正交:非 AWAKE 冻结一切动画与判定;NAP 中 5V 在,在场/手势可唤醒
//   (唤醒帧不判定,仿 busy_knobs 先例);DEEP 断 5V→单元复位,唤醒后重新
//   unit_gesture_init()(默认落在手势模式,无需再切模式)。
#include "magic_wand.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "bsp/m5stack_core_2.h"
#include "lvgl.h"

#include "audio_fx.h"
#include "core2_board.h"
#include "core2_sleep.h"
#include "haptics.h"
#include "imu_mpu6886.h"
#include "ledstrip_fx.h"
#include "unit_gesture.h"

#include "firefly.h"
#include "garden.h"

#include "tuning.h"

static const char *TAG = "magic_wand";

#define HINT_CARD  0xFFFFFF

// 手势音签播放期间近距音阶让位(SPEC §5.1):触发一个手势音签后这段时间内不响
// 档位变化的近距音阶。四音签(~4*55ms)封顶,留一点余量;纯实现细节,不在
// SPEC §10 tuning.h 列表(仿 firefly.c 外观常量的先例)。
#define GESTURE_AUDIO_YIELD_MS 300

typedef enum { ST_NO_UNIT = 0, ST_SEEK, ST_DANCE, ST_GOING_HOME } wand_state_t;

static wand_state_t s_state;

static bool s_unit_ok;
static int  s_retry_frames;
static int  s_retry_count;
static int  s_err_streak;

static uint32_t s_now_ms;          // 本局累计运行时长(帧 delay_ms 累加,非真实墙钟)

// ── 在场信号(SPEC §4.1)────────────────────────────────────────────────────
static bool     s_pres_ema_init;
static float    s_pres_ema;
static bool     s_pres_on;         // 迟滞开关后的在场状态
static int      s_level = 1;       // 强度档 1/2/3(远/中/近)
static bool     s_ever_seen;       // 本局是否曾经见过在场信号(首次入场必触发「你好」)
static uint32_t s_lost_since_ms;   // 最近一次"在场→离场"边沿的时刻(兼作保持期起点)

// ── 手势翻滚冷却(SPEC §4.2)────────────────────────────────────────────────
static uint32_t s_tumble_last_ms[GESTURE_WAVE + 1];   // 按 gesture_event_t 索引
static uint32_t s_gesture_audio_yield_until_ms;

static core2_sleep_t       s_sleep;
static core2_sleep_stage_t s_prev_stage = CORE2_SLEEP_AWAKE;

static lv_obj_t *s_plug_hint;
static lv_obj_t *s_hint_hand;

// ── 小工具 ───────────────────────────────────────────────────────────────
static lv_obj_t *plain(lv_obj_t *parent, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void anim_set_x(void *o, int32_t v) { lv_obj_set_x((lv_obj_t *)o, v); }

#if CALIB_LOG
static const char *state_name(wand_state_t s)
{
    switch (s) {
        case ST_NO_UNIT:    return "NO_UNIT";
        case ST_SEEK:       return "SEEK";
        case ST_DANCE:      return "DANCE";
        case ST_GOING_HOME: return "GOING_HOME";
        default:            return "?";
    }
}
#endif

// ── 在场强度档位(迟滞:粘住原档,防蹦档,SPEC §4.1)──────────────────────────
static int level_from_ema(int prev, float ema)
{
    int lvl = (ema >= PRES_LVL3_TH) ? 3 : (ema >= PRES_LVL2_TH) ? 2 : 1;
    if (prev == 3 && lvl < 3 && ema >= PRES_LVL3_TH - PRES_LVL_HYST) lvl = 3;
    if (prev >= 2 && lvl < 2 && ema >= PRES_LVL2_TH - PRES_LVL_HYST) lvl = 2;
    return lvl;
}

static uint16_t level_note(int lvl)
{
    switch (lvl) {
        case 1:  return 523;
        case 2:  return 659;
        default: return 784;
    }
}

// ── UI:无字提示卡(P4 灯笼未接入,pictogram 只画 Gesture 单元 + 一只挥动的手)──────
static void make_plug_hint(lv_obj_t *scr)
{
    lv_obj_t *card = plain(scr, 132, 86, HINT_CARD, 14);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t *sensor = plain(card, 46, 40, 0x8060C0, 8);
    lv_obj_align(sensor, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_t *lens = plain(sensor, 18, 18, 0xE8E0FF, LV_RADIUS_CIRCLE);
    lv_obj_align(lens, LV_ALIGN_CENTER, 0, 0);

    // 一只手在方块上方(两帧交替左右挥,path_step 离散跳变,不做缓动)
    lv_obj_t *hand = plain(card, 26, 18, 0xE0AD7A, 8);
    lv_obj_align(hand, LV_ALIGN_TOP_MID, -14, 8);
    s_hint_hand = hand;

    s_plug_hint = card;
    lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
}

static void hint_hand_wave(bool on)
{
    bsp_display_lock(0);
    lv_anim_delete(s_hint_hand, anim_set_x);
    if (on) {
        int x0 = lv_obj_get_x(s_hint_hand);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_hint_hand);
        lv_anim_set_exec_cb(&a, anim_set_x);
        lv_anim_set_values(&a, x0, x0 + 28);
        lv_anim_set_duration(&a, 480);
        lv_anim_set_playback_duration(&a, 480);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_step);   // 离散两帧交替
        lv_anim_start(&a);
    }
    bsp_display_unlock();
}

static void plug_hint_show(bool show)
{
    bsp_display_lock(0);
    if (show) lv_obj_remove_flag(s_plug_hint, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(s_plug_hint, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
    hint_hand_wave(show);
}

static void ui_create(void)
{
    lv_obj_t *scr;
    bsp_display_lock(0);
    scr = lv_screen_active();
    bsp_display_unlock();

    garden_create(scr);                                     // 自带锁,静态层画一次
    firefly_create(scr, GARDEN_HOME_X, GARDEN_HOME_Y);       // 自带锁

    bsp_display_lock(0);
    make_plug_hint(scr);
    bsp_display_unlock();
}

// ── 单元接管 / 缺席(仿 feed_monster/busy_knobs 惯例)──────────────────────
static bool unit_attach(bool greet)
{
    if (unit_gesture_init(core2_board_port_a(), 0) != ESP_OK) return false;
    // v2.1:默认落在手势模式即可用(9 手势 + 在场信号都在 bank0),不再调
    // unit_gesture_set_cursor_mode()(光标模式已实机否决,见 SPEC §0)。
    s_err_streak    = 0;
    s_unit_ok       = true;
    s_state         = ST_SEEK;
    s_pres_on       = false;
    s_pres_ema_init = false;
    s_level         = 1;
    firefly_enter_seek();
    plug_hint_show(false);
    if (greet) {
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
    }
    ESP_LOGI(TAG, "Gesture 已接管(手势模式)");
    return true;
}

static void unit_lost(void)
{
    s_unit_ok      = false;
    s_retry_frames = 0;
    s_state        = ST_NO_UNIT;
    plug_hint_show(true);
    audio_fx_play(SND_BUMP_MED);
    ESP_LOGW(TAG, "Gesture 失联(拔线/断电?),转入重试探测");
}

// ── 九手势 → 方向翻滚派发(仅 ST_DANCE 内消费,SPEC §4.2)────────────────────
static void dispatch_gesture(gesture_event_t g)
{
    if (g == GESTURE_NONE || g < 0 || g > GESTURE_WAVE) return;
    if (s_now_ms - s_tumble_last_ms[g] < TUMBLE_COOLDOWN_MS) return;   // 同方向冷却
    s_tumble_last_ms[g] = s_now_ms;

    switch (g) {
    case GESTURE_LEFT:
        firefly_tumble_left();
        audio_fx_play_notes((audio_note_t[]){ { 392, 50, 45 }, { 523, 50, 45 } }, 2);
        haptics_play(HAPTIC_BUMP_LIGHT);
        break;
    case GESTURE_RIGHT:
        firefly_tumble_right();
        audio_fx_play_notes((audio_note_t[]){ { 523, 50, 45 }, { 392, 50, 45 } }, 2);
        haptics_play(HAPTIC_BUMP_LIGHT);
        break;
    case GESTURE_UP:
        firefly_tumble_up();
        audio_fx_play_notes((audio_note_t[]){ { 523, 40, 45 }, { 659, 40, 45 }, { 784, 50, 45 } }, 3);
        haptics_play(HAPTIC_BUMP_LIGHT);
        break;
    case GESTURE_DOWN:
        firefly_tumble_down();
        audio_fx_play_notes((audio_note_t[]){ { 784, 40, 45 }, { 659, 40, 45 }, { 523, 50, 45 } }, 3);
        haptics_play(HAPTIC_BUMP_LIGHT);
        break;
    case GESTURE_FORWARD:
        firefly_tumble_forward();
        audio_fx_play_notes((audio_note_t[]){ { 440, 30, 50 }, { 880, 60, 55 } }, 2);
        haptics_play(HAPTIC_BUMP_LIGHT);
        break;
    case GESTURE_BACKWARD:
        firefly_tumble_backward();
        audio_fx_play_notes((audio_note_t[]){ { 440, 40, 45 }, { 330, 80, 40 } }, 2);
        haptics_play(HAPTIC_BUMP_LIGHT);
        break;
    case GESTURE_CLOCKWISE:
        firefly_tumble_cw();
        audio_fx_play_notes((audio_note_t[]){ { 523, 35, 45 }, { 659, 35, 45 }, { 784, 35, 45 }, { 988, 45, 50 } }, 4);
        haptics_play(HAPTIC_BUMP_LIGHT);
        break;
    case GESTURE_COUNTER_CLOCKWISE:
        firefly_tumble_ccw();
        audio_fx_play_notes((audio_note_t[]){ { 988, 35, 45 }, { 784, 35, 45 }, { 659, 35, 45 }, { 523, 45, 50 } }, 4);
        haptics_play(HAPTIC_BUMP_LIGHT);
        break;
    case GESTURE_WAVE:
        firefly_tumble_wave();
        audio_fx_play_notes((audio_note_t[]){ { 659, 45, 50 }, { 784, 45, 50 }, { 659, 45, 50 }, { 880, 60, 55 } }, 4);
        haptics_play(HAPTIC_COLLECT);
        break;
    default:
        return;
    }
    s_gesture_audio_yield_until_ms = s_now_ms + GESTURE_AUDIO_YIELD_MS;
    core2_sleep_kick(&s_sleep);   // 手势分类成功 = 有人在玩(SPEC §7)
}

// ── 在场/手势轮询(仿 feed_monster poll_sonic 的接管/重试骨架)──────────────
static void poll_presence(core2_sleep_stage_t stage)
{
    if (!s_unit_ok) {
        if (++s_retry_frames >= ATTACH_RETRY_MS / PRES_POLL_MS) {
            s_retry_frames = 0;
            if (unit_attach(true)) {
                s_retry_count = 0;
            } else if (++s_retry_count % 15 == 0) {
                bool found = core2_board_port_a_scan();
                if (found || core2_board_port_a_stuck()) core2_board_port_a_recover();
            }
        }
        return;
    }

    uint8_t  brightness = 0;
    uint16_t size = 0;
    gesture_event_t gesture = GESTURE_NONE;
    esp_err_t r_pres = unit_gesture_read_presence(&brightness, &size);
    esp_err_t r_ges  = unit_gesture_read(&gesture);

    // 每帧轮询两样(在场 + 手势),两者都读失败才算拔线(见任务纪律)。
    if (r_pres != ESP_OK && r_ges != ESP_OK) {
        if (++s_err_streak >= ERR_STREAK_LOST) unit_lost();
        return;
    }
    s_err_streak = 0;
    if (r_ges != ESP_OK) gesture = GESTURE_NONE;

    bool pres_valid = (r_pres == ESP_OK);
    if (pres_valid) {
        if (!s_pres_ema_init) {
            s_pres_ema = brightness;
            s_pres_ema_init = true;
        } else {
            s_pres_ema += (brightness - s_pres_ema) * (PRES_EMA_PCT / 100.0f);
        }
    }

#if CALIB_LOG
    // 标定日志(SPEC §9/§13):每 ~300ms 打一行,回填 tuning.h 的 PRES_* 阈值后
    // 置 CALIB_LOG=0 重编。寄存器语义未标定,这里只打印读数,不做任何"确定"断言。
    {
        static uint32_t last_log_ms;
        if (s_now_ms - last_log_ms >= 300) {
            last_log_ms = s_now_ms;
            ESP_LOGI(TAG, "CALIB pres raw=%u ema=%u size=%u lvl=%d state=%s",
                     brightness, (unsigned)s_pres_ema, size, s_level, state_name(s_state));
        }
    }
#endif

    if (!pres_valid) {
        // 只有手势读到了:在场沿用上一帧判定,不重复走迟滞;手势该消费还消费。
        if (stage == CORE2_SLEEP_AWAKE && s_state == ST_DANCE) dispatch_gesture(gesture);
        return;
    }

    bool was_on = s_pres_on;
    if (!s_pres_on && s_pres_ema >= PRES_ON_TH)      s_pres_on = true;
    else if (s_pres_on && s_pres_ema <= PRES_OFF_TH) s_pres_on = false;

    if (s_pres_on) {
        // 真实的手在场 = 有人在玩,顶住/唤醒(SPEC §7)。
        core2_sleep_kick(&s_sleep);
        if (stage != CORE2_SLEEP_AWAKE) {
            core2_sleep_wake(&s_sleep);
            return;   // 唤醒帧不判定(仿 busy_knobs 先例),下一帧才正式进状态机
        }
    }
    if (stage != CORE2_SLEEP_AWAKE) return;   // 非清醒态不判定状态机

    bool rising_edge  = (!was_on && s_pres_on);
    bool falling_edge = (was_on && !s_pres_on);
    if (falling_edge) s_lost_since_ms = s_now_ms;   // 兼作"保持期起点"与"「你好」间隔起点"

    if (rising_edge) {
        bool hello = !s_ever_seen || (s_now_ms - s_lost_since_ms >= HELLO_GAP_MS);
        s_ever_seen = true;

        if (s_state == ST_GOING_HOME) firefly_go_home_interrupt();
        firefly_enter_dance();
        s_state = ST_DANCE;

        if (hello) {
            audio_fx_play_notes((audio_note_t[]){ { 659, 40, 50 }, { 880, 60, 55 } }, 2);
            haptics_play(HAPTIC_HELLO);
            ESP_LOGI(TAG, "「你好」时刻");
        }
    }

    if (s_state != ST_DANCE) return;

    int lvl = s_pres_on ? level_from_ema(s_level, s_pres_ema) : 1;   // 保持期强制最低档
    if (lvl != s_level) {
        s_level = lvl;
        if (s_now_ms >= s_gesture_audio_yield_until_ms) {   // 手势音签播放期间让位
            uint16_t note = level_note(lvl);
            audio_fx_play_notes((audio_note_t[]){ { note, 40, 35 } }, 1);
        }
    }

    if (!s_pres_on && (s_now_ms - s_lost_since_ms >= PRES_HOLD_MS)) {
        // 离场保持耗尽:挥手再见(复用 WAVE 翻滚)+ 降双音(无震)→ 回家
        firefly_tumble_wave();
        audio_fx_play_notes((audio_note_t[]){ { 880, 50, 45 }, { 659, 60, 45 } }, 2);
        firefly_go_home_start();
        s_state = ST_GOING_HOME;
        ESP_LOGI(TAG, "离场保持耗尽,回家");
        return;
    }

    dispatch_gesture(gesture);
}

// ── 主任务(30Hz,SPEC §8)──────────────────────────────────────────────
static void game_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        imu_accel_t acc;
        bool have = (imu_mpu6886_read_accel(&acc) == ESP_OK);

        int delay_ms = core2_sleep_feed(&s_sleep, have ? (float[]){ acc.x, acc.y, acc.z } : NULL, have);
        core2_sleep_stage_t stage = core2_sleep_stage(&s_sleep);
        s_now_ms += (uint32_t)delay_ms;

        // 深度省电切过 M-Bus 5V → Gesture 单元掉电复位:醒来后重新接管(默认手势模式)
        if (s_prev_stage == CORE2_SLEEP_DEEP && stage != CORE2_SLEEP_DEEP) {
            s_unit_ok = false;
            unit_attach(false);
        }
        // 唤醒(NAP/DEEP → AWAKE):回 SEEK 重新判定(仿 busy_knobs 先例)
        if (s_prev_stage != CORE2_SLEEP_AWAKE && stage == CORE2_SLEEP_AWAKE) {
            s_state   = ST_SEEK;
            s_pres_on = false;
            s_pres_ema_init = false;
            firefly_enter_seek();
        }
        // 入睡(AWAKE → NAP/DEEP):冻结外观动画(SPEC §3:非 AWAKE 冻结一切动画/判定)
        if (s_prev_stage == CORE2_SLEEP_AWAKE && stage != CORE2_SLEEP_AWAKE) {
            firefly_freeze();
        }
        s_prev_stage = stage;

        if (stage != CORE2_SLEEP_DEEP) poll_presence(stage);

        if (stage == CORE2_SLEEP_AWAKE) {
            if (s_state == ST_DANCE) firefly_dance_advance((uint32_t)delay_ms, s_level);
            if (s_state == ST_GOING_HOME && firefly_go_home_is_done()) {
                s_state = ST_SEEK;
            }
            firefly_tick();
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms));
    }
}

void magic_wand_start(void)
{
    ui_create();

    core2_sleep_cfg_t scfg = CORE2_SLEEP_CFG_DEFAULT;
    scfg.frame_ms = PRES_POLL_MS;   // 30Hz,与在场轮询同拍(SPEC §8/§10)
    core2_sleep_init(&s_sleep, &scfg);

    ledstrip_fx_set_base(LED_BASE_AMBIENT);

    bool attached = unit_attach(false);
    if (!attached) {
        ESP_LOGW(TAG, "Gesture 未就位:必须插 Core2 机身侧面的红色 PORT.A 口"
                      "(底座黑口 PORT.B/蓝口 PORT.C 不是 I2C)");
        bool found = core2_board_port_a_scan();
        if (found || core2_board_port_a_stuck()) {
            core2_board_port_a_recover();
            attached = unit_attach(false);
            if (!attached) core2_board_port_a_scan();
        }
    }
    if (attached) {
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
    } else {
        s_state = ST_NO_UNIT;
        plug_hint_show(true);
        audio_fx_play(SND_BUMP_MED);
    }

    xTaskCreate(game_task, "magicwand", 4096, NULL, 5, NULL);
}
