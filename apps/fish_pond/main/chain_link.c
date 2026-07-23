// chain_link —— Chain 传输/绑定层 + 30Hz game_task(SPEC.md §8;摇杆回中修复照抄 chain_lab 定案)
//
// 只做"读节点、算增量/归一化、判活动/掉线、扫描认领",不碰渲染/玩法——渲染/玩法在
// pond/boat/fish/bucket 四个模块。本文件是 chain_lab.c 转正化的对应物(去掉诊断 UI 与
// 节点 RGB 写入,SPEC §7 反馈矩阵未列节点灯这一列)。
#include "chain_link.h"

#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "bsp/m5stack_core_2.h"
#include "lvgl.h"

#include "audio_fx.h"
#include "core2_sleep.h"
#include "haptics.h"
#include "imu_mpu6886.h"
#include "ledstrip_fx.h"

#include "chain_bus.h"
#include "unit_chain_encoder.h"
#include "unit_chain_joystick.h"

#include "boat.h"
#include "bucket.h"
#include "fish.h"
#include "pond.h"
#include "sprites.h"
#include "tuning.h"

static const char *TAG = "fish_pond";

// ── 传输层状态 ───────────────────────────────────────────────────────
static uint8_t s_enc_id;
static uint8_t s_joy_id;
static int16_t s_enc_value;
static int16_t s_enc_delta;      // 本帧增量(消费即清零,SPEC §8"chain_link.c 算好增量")
static int     s_enc_streak;

static float   s_joy_cx = 2048.f, s_joy_cy = 2048.f;   // 居中校准(持续自适应回中)
static float   s_joy_nx, s_joy_ny;
static int     s_joy_streak;
static uint16_t s_joy_px, s_joy_py;
static bool     s_joy_have_prev;

static int     s_rescan_frames;

static core2_sleep_t       s_sleep;
static core2_sleep_stage_t s_prev_stage = CORE2_SLEEP_AWAKE;

// ── 无字提示卡(没认到任何 Chain 节点时显示;小船+插头图,SPEC §1)──────────────
static lv_obj_t *s_hint_card;
static bool      s_hint_shown;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void hint_card_create(lv_obj_t *scr)
{
    bsp_display_lock(0);
    s_hint_card = fpond_box(scr, 132, 76, COL_HINT_CARD, 14);
    lv_obj_align(s_hint_card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *socket = fpond_box(s_hint_card, 30, 30, 0xE0DACB, LV_RADIUS_CIRCLE);
    lv_obj_align(socket, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *hole = fpond_box(socket, 12, 12, 0x8A8272, LV_RADIUS_CIRCLE);
    lv_obj_center(hole);
    lv_obj_t *wire = fpond_box(s_hint_card, 34, 4, 0x3A3A38, 2);
    lv_obj_align(wire, LV_ALIGN_LEFT_MID, 48, 0);
    lv_obj_t *plug = fpond_box(s_hint_card, 16, 22, 0x3A3A38, 4);
    lv_obj_align(plug, LV_ALIGN_LEFT_MID, 82, 0);
    lv_obj_add_flag(s_hint_card, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
    s_hint_shown = false;
}

static void hint_card_apply(bool show)
{
    if (show == s_hint_shown) return;
    s_hint_shown = show;
    bsp_display_lock(0);
    if (show) lv_obj_remove_flag(s_hint_card, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(s_hint_card, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
}

static void sync_attach(void)
{
    hint_card_apply(!s_enc_id && !s_joy_id);
}

// ── 探测 / 绑定(同 chain_lab scan_bus,去掉诊断问候之外的 UI 联动)──────────────
static void joy_calibrate_center(uint8_t id)
{
    uint32_t sx = 0, sy = 0; int ok = 0;
    for (int i = 0; i < 6; i++) {
        uint16_t x, y;
        if (unit_chain_joystick_read_adc(id, &x, &y) == ESP_OK) { sx += x; sy += y; ok++; }
        vTaskDelay(pdMS_TO_TICKS(8));
    }
    if (ok) { s_joy_cx = (float)sx / ok; s_joy_cy = (float)sy / ok; }
    else    { s_joy_cx = 2048.f;         s_joy_cy = 2048.f;         }
    s_joy_have_prev = false;
}

static void scan_bus(bool greet)
{
    for (uint8_t id = 1; id <= CHAIN_MAX_ID; id++) {
        if ((s_enc_id == id) || (s_joy_id == id)) continue;
        chain_dev_type_t type;
        if (chain_bus_get_device_type(id, &type, 60) != ESP_OK) continue;
        ESP_LOGI(TAG, "链位 %u:设备类型 0x%04X", id, type);
        if (type == CHAIN_DEV_ENCODER && !s_enc_id) {
            s_enc_id = id; s_enc_streak = 0; s_enc_delta = 0;
            int16_t v;
            if (unit_chain_encoder_read_value(id, &v) == ESP_OK) s_enc_value = v;
            ESP_LOGI(TAG, "Chain Encoder(曲柄)接管 @链位%u", id);
        } else if (type == CHAIN_DEV_JOYSTICK && !s_joy_id) {
            s_joy_id = id; s_joy_streak = 0;
            joy_calibrate_center(id);
            ESP_LOGI(TAG, "Chain Joystick(船)接管 @链位%u", id);
        }
    }

    if (!s_enc_id && !s_joy_id) {
        ESP_LOGW(TAG, "PORT.C 上没认到 Chain 节点。排查:①插蓝口 PORT.C ②Chain Bridge 箭头朝主控 "
                      "③节点 5V(底座灯带亮=5V 有电)");
        chain_bus_sniff(300);
    } else if (greet) {
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
    }
}

// ── 轮询(SPEC §8:增量/归一化在本层算好,boat.c 只消费结果)────────────────────
static bool poll_enc(void)
{
    if (!s_enc_id) { s_enc_delta = 0; return false; }
    int16_t v;
    if (unit_chain_encoder_read_value(s_enc_id, &v) != ESP_OK) {
        if (++s_enc_streak >= ERR_STREAK_LOST) {
            ESP_LOGW(TAG, "曲柄(Encoder)失联(拔线/断电?)");
            s_enc_id = 0;
        }
        s_enc_delta = 0;
        return false;
    }
    s_enc_streak = 0;
    s_enc_delta = (int16_t)(v - s_enc_value);
    s_enc_value = v;
    return s_enc_delta != 0;
}

static bool poll_joy(void)
{
    if (!s_joy_id) { s_joy_nx = 0; s_joy_ny = 0; return false; }
    uint16_t x, y;
    if (unit_chain_joystick_read_adc(s_joy_id, &x, &y) != ESP_OK) {
        if (++s_joy_streak >= ERR_STREAK_LOST) {
            ESP_LOGW(TAG, "船(Joystick)失联(拔线/断电?)");
            s_joy_id = 0;
        }
        s_joy_nx = 0; s_joy_ny = 0;
        return false;
    }
    s_joy_streak = 0;

    int dx = 0, dy = 0;
    if (s_joy_have_prev) {
        dx = (int)x - (int)s_joy_px; if (dx < 0) dx = -dx;
        dy = (int)y - (int)s_joy_py; if (dy < 0) dy = -dy;
    }
    s_joy_px = x; s_joy_py = y; s_joy_have_prev = true;

    float rx = ((float)x - s_joy_cx) / JOY_HALF_SPAN;
    float ry = ((float)y - s_joy_cy) / JOY_HALF_SPAN;

    // 自适应回中(chain_lab 定案照抄):只在偏移还在回中带内时慢慢拉中心过去
    if (fabsf(rx) < JOY_RECENTER_BAND) s_joy_cx += ((float)x - s_joy_cx) * JOY_RECENTER_PCT / 100.0f;
    if (fabsf(ry) < JOY_RECENTER_BAND) s_joy_cy += ((float)y - s_joy_cy) * JOY_RECENTER_PCT / 100.0f;

#if JOY_SWAP_XY
    float tmp = rx; rx = ry; ry = tmp;
#endif
#if JOY_INVERT_X
    rx = -rx;
#endif
#if JOY_INVERT_Y
    ry = -ry;
#endif
    s_joy_nx = clampf(rx, -1.0f, 1.0f);
    s_joy_ny = clampf(ry, -1.0f, 1.0f);

    bool moved = (dx > JOY_MOVE_ADC) || (dy > JOY_MOVE_ADC)
                 || fabsf(s_joy_nx) > JOY_HOLD_KICK || fabsf(s_joy_ny) > JOY_HOLD_KICK;
    return moved;
}

// ── 30Hz 主任务 ──────────────────────────────────────────────────────
static void game_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        imu_accel_t acc;
        bool have = (imu_mpu6886_read_accel(&acc) == ESP_OK);
        int delay_ms = core2_sleep_feed(&s_sleep,
                                        have ? (float[]){ acc.x, acc.y, acc.z } : NULL, true);
        core2_sleep_stage_t stage = core2_sleep_stage(&s_sleep);

        // 深度省电切过 M-Bus 5V → Chain 节点掉电复位:醒来重扫(桌面玩法坑,SPEC §10)
        if (s_prev_stage == CORE2_SLEEP_DEEP && stage != CORE2_SLEEP_DEEP) {
            s_enc_id = 0; s_joy_id = 0;
            scan_bus(false);
            sync_attach();
        }
        s_prev_stage = stage;

        if (stage != CORE2_SLEEP_DEEP) {
            bool prev_bound = s_enc_id || s_joy_id;

            if (!s_enc_id || !s_joy_id) {   // 有空位就低频重扫(认新插入/恢复失联)
                if (++s_rescan_frames >= ATTACH_RETRY_MS / POLL_PERIOD_MS) {
                    s_rescan_frames = 0;
                    scan_bus(true);
                }
            }

            bool act = poll_enc();
            act |= poll_joy();

            if ((s_enc_id || s_joy_id) != prev_bound) sync_attach();

            if (act) {
                core2_sleep_kick(&s_sleep);
                if (stage != CORE2_SLEEP_AWAKE) core2_sleep_wake(&s_sleep);
            }

            if (stage == CORE2_SLEEP_AWAKE) {
                boat_tick(POLL_PERIOD_MS, s_joy_nx, s_enc_delta);
                fish_tick(POLL_PERIOD_MS);
                bucket_tick(POLL_PERIOD_MS);
            }
            s_enc_delta = 0;   // 增量只对本帧有效,消费完清零
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms));
    }
}

void chain_link_start(void)
{
    lv_obj_t *scr = lv_screen_active();

    pond_create(scr);
    boat_create(scr);
    fish_create(scr);
    bucket_create(scr);
    hint_card_create(scr);

    core2_sleep_cfg_t scfg = CORE2_SLEEP_CFG_DEFAULT;
    scfg.nap_after_ms     = NAP_AFTER_MS;
    scfg.deep_after_ms    = DEEP_AFTER_MS;
    scfg.awake_brightness = PLAY_BRIGHTNESS;
    scfg.nap_brightness   = NAP_BRIGHTNESS;
    scfg.frame_ms         = POLL_PERIOD_MS;
    core2_sleep_init(&s_sleep, &scfg);

    ledstrip_fx_set_base(LED_BASE_AMBIENT);

    esp_err_t err = chain_bus_init_port_c();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "chain_bus 初始化失败:%s", esp_err_to_name(err));
    } else {
        scan_bus(false);
    }
    sync_attach();

    if (s_enc_id || s_joy_id) {
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
    } else {
        audio_fx_play(SND_BUMP_MED);   // 没认到:温柔一声"咦?",屏上有提示卡
    }

    xTaskCreate(game_task, "fish_pond", 5120, NULL, 5, NULL);
}
