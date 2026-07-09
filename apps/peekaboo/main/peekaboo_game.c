// 躲猫猫昼夜屋 v2「夜里来客」—— 游戏主体(状态编排;传感层/省电/单元容错见 SPEC §2 保留项)
//
// 模型不变:DLight 测环境光 lux,自适应基准 s_ref 判"捂住/松开"(见 tuning.h 注释)。
// v2 把"昼夜二值 + 揭晓计数"换成"访客池 + 夜悬念 + 相册收集 + 游行"(见 SPEC.md)。
//
// 本文件只做**编排**:抽签/悬念节拍/揭晓变体/游行 的具体视觉都在 scene.c / visitor.c /
// album.c;本文件负责状态机、传感层扩展(§4:速度档/夜时长/黄昏驻留)、四通道事件分派、
// 冲突矩阵优先级(§15)、省电/单元容错(v1 保留)。
#include "peekaboo_game.h"

#include <math.h>

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
#include "unit_dlight.h"

#include "album.h"
#include "scene.h"
#include "visitor.h"

#include "tuning.h"

static const char *TAG = "peekaboo";

// ── 传感层状态(v1 保留)──────────────────────────────────────────────────
static bool  s_night_logical;   // true = 逻辑夜(已抽签,悬念/揭晓待定)
static bool  s_first;
static float s_lux, s_ref, s_last_lux;

static bool s_unit_ok;
static int  s_retry_frames, s_retry_count, s_err_streak, s_since_read;

// ── 传感层扩展(§4)───────────────────────────────────────────────────────
static int s_band_reads;        // 迟滞带内连续读数(松开速度档 §4.2 + 黄昏驻留 §9 共用)
static int s_night_frames;      // AWAKE 且逻辑夜的帧计数(长夜 §4.3)
static int s_current_visitor = -1;

// ── 游行 / 首遇入册(延时反馈,见 §15)─────────────────────────────────────
static bool s_parade_active;
static int  s_parade_frames_left;
static bool s_parade_pending;
static int  s_parade_lead_frames;
static int  s_collect_pending_id = -1;
static int  s_collect_frames_left;
static int  s_gaze_frames;

static int  s_led_base = -1;

static core2_sleep_t       s_sleep;
static core2_sleep_stage_t s_prev_stage = CORE2_SLEEP_AWAKE;

static lv_obj_t *s_plug_hint;   // 无字提示卡(v1 保留)

// ── 无字提示卡(v1 保留,原样)──────────────────────────────────────────────
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

static void make_plug_hint(lv_obj_t *scr)
{
    lv_obj_t *card = plain(scr, 132, 76, 0xFFFFFF, 14);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *sensor = plain(card, 42, 42, 0xFFC75F, 8);
    lv_obj_align(sensor, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *eye = plain(sensor, 16, 16, 0xFFF3B0, LV_RADIUS_CIRCLE);
    lv_obj_center(eye);
    lv_obj_t *wire = plain(card, 30, 4, 0x3A3A38, 2);
    lv_obj_align(wire, LV_ALIGN_LEFT_MID, 60, 0);
    lv_obj_t *plug = plain(card, 16, 22, 0x3A3A38, 4);
    lv_obj_align(plug, LV_ALIGN_LEFT_MID, 94, 0);
    s_plug_hint = card;
    lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
}

static void plug_hint_show(bool show)
{
    bsp_display_lock(0);
    if (show) lv_obj_remove_flag(s_plug_hint, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(s_plug_hint, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
}

// ── UI 搭建 ───────────────────────────────────────────────────────────
static void ui_create(void)
{
    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    bsp_display_unlock();

    scene_create(scr);
    visitor_create(scr);
    album_create(scr);

    bsp_display_lock(0);
    make_plug_hint(scr);
    bsp_display_unlock();
}

static void apply_led_base(void)
{
    led_base_t b = s_night_logical ? LED_BASE_IDLE : LED_BASE_AMBIENT;
    if ((int)b != s_led_base) {
        s_led_base = (int)b;
        ledstrip_fx_set_base(b);
    }
}

static inline dawn_speed_t classify_speed(int band_reads)
{
    if (band_reads <= DAWN_FAST_READS) return DAWN_FAST;
    if (band_reads >= DAWN_SLOW_READS) return DAWN_SLOW;
    return DAWN_NORMAL;
}

// 首遇入册落地(§7:出场收尾时相册头像点亮 + SND_COLLECT + HAPTIC_COLLECT)。
// go_day() 在连打打断前一位待入册时也会调它:抢先落地,不因下一次揭晓覆盖而丢失(§15 规则 3)。
static void finish_pending_collect(void)
{
    if (s_collect_pending_id < 0) return;
    int id = s_collect_pending_id;
    s_collect_pending_id = -1;
    audio_fx_play(SND_COLLECT);
    haptics_play(HAPTIC_COLLECT);
    album_mark_collected(id);
    if (album_count() >= VISITOR_POOL_N && !s_parade_active && !s_parade_pending) {
        s_parade_pending     = true;
        s_parade_lead_frames = PARADE_LEAD_IN_MS / POLL_PERIOD_MS;
    }
}

// ── 昼夜(逻辑)转换 ───────────────────────────────────────────────────────
static void go_night(void)
{
    s_night_logical = true;
    s_night_frames  = 0;
    scene_night_ambience_reset();
    visitor_hide_all_instant();

    int id = visitor_draw_for_night(album_mask(), album_last_full_round_id());
    s_current_visitor = id;
    visitor_tease_start(id);

    audio_fx_play_notes((audio_note_t[]){ { 587, 130, 45 }, { 440, 170, 40 } }, 2);
    haptics_play(HAPTIC_BUMP_LIGHT);
    apply_led_base();
    ESP_LOGI(TAG, "捂住 → 入夜,访客=%d(lux %.0f / ref %.0f)", id, s_lux, s_ref);
}

static void go_day(int band_reads_before)
{
    dawn_speed_t speed  = classify_speed(band_reads_before);
    bool long_night     = (s_night_frames * POLL_PERIOD_MS / 1000) >= NIGHT_LONG_S;
    int  id              = s_current_visitor;
    bool is_rainbow      = (id == VISITOR_RAINBOW);

    s_night_logical = false;

    bool first_time  = !is_rainbow && !album_is_collected(id);
    int  entrance_ms = visitor_reveal(id, speed, first_time);

    if (is_rainbow) {
        scene_burst(0xFFE89B, BURST_COUNT);
        haptics_play(HAPTIC_WIN);
        ledstrip_fx_trigger(LED_FX_WIN);
        scene_char_bounce(20, 160, 200);
    } else {
        haptics_play(speed == DAWN_FAST ? HAPTIC_BUMP_MED : speed == DAWN_SLOW ? HAPTIC_HELLO : HAPTIC_WAKE);
        ledstrip_fx_trigger(speed == DAWN_FAST ? LED_FX_FLASH : speed == DAWN_SLOW ? LED_FX_GATHER : LED_FX_COLLECT);
        scene_char_bounce(speed == DAWN_FAST ? 24 : 16,
                           speed == DAWN_FAST ? 110 : 150,
                           speed == DAWN_FAST ? 140 : 190);
    }
    apply_led_base();

    if (long_night) {
        scene_dream_butterflies_trigger();
        audio_fx_play_notes((audio_note_t[]){ { 523, 60, 40 }, { 659, 60, 40 }, { 784, 90, 40 } }, 3);
    }

    s_gaze_frames = GAZE_MS / POLL_PERIOD_MS;

    if (first_time) {
        finish_pending_collect();   // 连打打断前一位待入册:抢先落地,不被本次覆盖丢失
        s_collect_pending_id  = id;
        s_collect_frames_left = entrance_ms / POLL_PERIOD_MS;
        if (s_collect_frames_left < 1) s_collect_frames_left = 1;
    }

    ESP_LOGI(TAG, "松开 → 天亮!访客=%d 速度=%d 长夜=%d(lux %.0f / ref %.0f)",
             id, speed, long_night, s_lux, s_ref);
    s_current_visitor = -1;
}

static void start_parade(void)
{
    s_parade_active = true;
    int total = visitor_parade_start();
    s_parade_frames_left = total / POLL_PERIOD_MS;
    audio_fx_play(SND_WIN);
    haptics_play(HAPTIC_WIN);
    ledstrip_fx_trigger(LED_FX_WIN);
    scene_char_bounce(24, 180, 220);
    scene_burst(0xFFE89B, BURST_COUNT);
    ESP_LOGI(TAG, "集齐 6 位 → 游行!");
}

// 游行收场:按当前照度重评昼夜(§15 规则 1;捂着就直接入夜走悬念)
static void reevaluate_after_parade(void)
{
    s_band_reads = 0;
    if (s_lux < s_ref * COVER_FRAC) {
        go_night();
        scene_apply(SCENE_NIGHT);
    } else {
        scene_apply(SCENE_DAY);
    }
}

// ── 单元接管 / 缺席(v1 保留,原样)─────────────────────────────────────────
static bool unit_attach(bool greet)
{
    if (unit_dlight_init(core2_board_port_a(), 0) != ESP_OK) return false;
    s_since_read = 0;
    s_err_streak = 0;
    s_unit_ok    = true;
    s_first      = true;
    plug_hint_show(false);
    if (greet) {
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
    }
    ESP_LOGI(TAG, "DLight 已接管");
    return true;
}

static void unit_lost(void)
{
    s_unit_ok      = false;
    s_retry_frames = 0;
    plug_hint_show(true);
    audio_fx_play(SND_BUMP_MED);
    ESP_LOGW(TAG, "DLight 失联(拔线/断电?),转入重试探测");
}

// ── 一次新照度读数:更新基准 + 昼夜/黄昏判定(§4/§9)─────────────────────────
static void on_lux(float lux, core2_sleep_stage_t stage)
{
    if (s_first) {
        s_lux      = lux;
        s_ref      = lux > REF_MIN_LUX ? lux : REF_MIN_LUX;
        s_last_lux = lux;
        s_first    = false;
    } else {
        s_lux += (lux - s_lux) * LUX_ALPHA;
    }

    if (s_lux > s_ref) s_ref += (s_lux - s_ref) * REF_RISE;
    else                s_ref += (s_lux - s_ref) * REF_FALL;
    if (s_ref < REF_MIN_LUX) s_ref = REF_MIN_LUX;

    if (fabsf(s_lux - s_last_lux) > s_ref * MOVE_FRAC) {
        core2_sleep_kick(&s_sleep);
        if (stage != CORE2_SLEEP_AWAKE) core2_sleep_wake(&s_sleep);
    }
    s_last_lux = s_lux;

    if (stage != CORE2_SLEEP_AWAKE) return;
    if (s_parade_active) return;   // §15 规则 1:游行期间照度转换挂起(只记当前带,不切场景)

    int  band_reads_before = s_band_reads;
    bool in_band = (s_lux > s_ref * COVER_FRAC) && (s_lux < s_ref * UNCOVER_FRAC);
    s_band_reads = in_band ? s_band_reads + 1 : 0;

    if (!s_night_logical && s_lux < s_ref * COVER_FRAC) {
        go_night();
    } else if (s_night_logical && s_lux > s_ref * UNCOVER_FRAC) {
        go_day(band_reads_before);
    }

    // 黄昏视觉叠加(§9;纯装饰,不改上面任何逻辑判定)
    scene_kind_t want = s_night_logical ? SCENE_NIGHT : SCENE_DAY;
    if (DUSK_ENABLED && in_band && s_band_reads >= DUSK_DWELL_READS) want = SCENE_DUSK;
    if (want != scene_current()) {
        scene_apply(want);
        if (want == SCENE_DUSK) audio_fx_play_notes((audio_note_t[]){ { 494, 140, 35 } }, 1);
    }
}

// ── DLight 轮询(v1 保留,原样)──────────────────────────────────────────
static void poll_dlight(core2_sleep_stage_t stage)
{
    if (!s_unit_ok) {
        if (++s_retry_frames >= UNIT_RETRY_MS / POLL_PERIOD_MS) {
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

    if (++s_since_read < DLIGHT_READ_TICKS) return;
    s_since_read = 0;

    float lux = 0;
    esp_err_t r = unit_dlight_read_lux(&lux);
    if (r == ESP_OK) {
        s_err_streak = 0;
        on_lux(lux, stage);
    } else {
        if (++s_err_streak >= ERR_STREAK_LOST) unit_lost();
    }
}

// ── 主任务(30Hz)──────────────────────────────────────────────────────
static void game_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        imu_accel_t acc;
        bool have = (imu_mpu6886_read_accel(&acc) == ESP_OK);

        int delay_ms = core2_sleep_feed(&s_sleep,
                                        have ? (float[]){ acc.x, acc.y, acc.z } : NULL,
                                        have);
        core2_sleep_stage_t stage = core2_sleep_stage(&s_sleep);
        core2_sleep_stage_t prev  = s_prev_stage;

        // 深度省电切过 M-Bus 5V → DLight 掉电复位:醒来后重新接管(重配连续模式)
        if (prev == CORE2_SLEEP_DEEP && stage != CORE2_SLEEP_DEEP) {
            s_unit_ok = false;
            unit_attach(false);
        }
        // 打盹/深度省电唤醒回 AWAKE:悬念节拍重置(眼睛若已出现则保持,§15 规则 4)
        if (prev != CORE2_SLEEP_AWAKE && stage == CORE2_SLEEP_AWAKE && s_night_logical) {
            int base_ms = visitor_tease_wake_resume(s_current_visitor);
            s_night_frames = base_ms / POLL_PERIOD_MS;
        }
        s_prev_stage = stage;

        if (stage != CORE2_SLEEP_AWAKE) s_led_base = -1;

        if (stage != CORE2_SLEEP_DEEP) poll_dlight(stage);

        if (stage == CORE2_SLEEP_AWAKE) {
            apply_led_base();

            // 拔线时下面全部冻结(§15 规则 5):不喂夜氛围/不推进游行/不揭晓入册
            if (s_unit_ok) {
                if (s_night_logical) {
                    s_night_frames++;
                    int elapsed = s_night_frames * POLL_PERIOD_MS;
                    visitor_tease_tick(s_current_visitor, elapsed);
                    scene_night_ambience_tick(elapsed);
                }

                if (s_gaze_frames > 0 && --s_gaze_frames == 0) scene_char_gaze(0);

                if (s_collect_pending_id >= 0 && --s_collect_frames_left <= 0) {
                    finish_pending_collect();
                }
                if (s_parade_pending && --s_parade_lead_frames <= 0) {
                    s_parade_pending = false;
                    start_parade();
                }
                if (s_parade_active && --s_parade_frames_left <= 0) {
                    s_parade_active = false;
                    visitor_parade_reset();
                    album_reset();
                    reevaluate_after_parade();
                }
            }
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms));
    }
}

void peekaboo_game_start(void)
{
    s_night_logical = false;
    s_first         = true;
    s_led_base      = -1;

    ui_create();

    core2_sleep_cfg_t scfg = CORE2_SLEEP_CFG_DEFAULT;
    scfg.nap_after_ms     = NAP_AFTER_MS;
    scfg.deep_after_ms    = DEEP_AFTER_MS;
    scfg.awake_brightness = PLAY_BRIGHTNESS;
    scfg.nap_brightness   = NAP_BRIGHTNESS;
    scfg.frame_ms         = POLL_PERIOD_MS;
    core2_sleep_init(&s_sleep, &scfg);

    ledstrip_fx_set_base(LED_BASE_AMBIENT);
    s_led_base = (int)LED_BASE_AMBIENT;

    bool attached = unit_attach(false);
    if (!attached) {
        ESP_LOGW(TAG, "DLight 未就位:必须插 Core2 机身侧面的红色 PORT.A 口"
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
        plug_hint_show(true);
        audio_fx_play(SND_BUMP_MED);
    }

    xTaskCreate(game_task, "peekaboo", 5120, NULL, 5, NULL);
}
