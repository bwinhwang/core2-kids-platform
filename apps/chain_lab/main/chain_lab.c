// Chain 验证台 —— 主体(UI + 20Hz 轮询 + 反馈编排)
//
// 左半 = Chain Encoder:圆环表盘 + orbiting 指针(角度=绝对计数)+ 中心数值;转 → 指针转、
//   数值变、节点板载 RGB 随计数换色(彩虹环);按中心键 → 指针变绿 + 节点灯闪白 + 轻震。
// 右半 = Chain Joystick:方框 + 光点(位置=归一化 X/Y)+ 原始 ADC 数值;推 → 光点走、节点
//   灯随方位换色;按下 Z → 光点变绿 + 节点灯闪白 + 轻震。
// 同时验证:读(GET_VALUE/16ADC/BUTTON)与写(SET_RGB)两条路都通,即证明 Core2 直连
//   Chain host + 两个驱动 + chain_bus 传输层成立。
//
// 节点直连时链上位置=1;两节点级联时按物理顺序 1、2——本台开机扫 1..CHAIN_MAX_ID 自动认。
// 供电/接线/省电坑同 chain_bus 头注释(PORT.C 5V=EXTEN;深度省电切 5V→节点复位→重扫)。

#include "chain_lab.h"

#include <math.h>
#include <string.h>

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

#include "chain_bus.h"
#include "unit_chain_encoder.h"
#include "unit_chain_joystick.h"

#include "crane_game.h"

#include "tuning.h"

static const char *TAG = "chain_lab";

#if CHAIN_LAB_DIAG_MODE
// ── 隐藏诊断台 UI(SPEC.md §7 方案B:仅 CHAIN_LAB_DIAG_MODE=1 编译期参与)──────────

#define SCREEN_W 320
#define SCREEN_H 240

// ── 配色(§18 暖色家族 + 仪表台)──────────────────────────────────────
#define BG_COL      0xEAE6DA
#define TEXT_COL    0x3A3A38
#define PANEL_COL   0xFBF7EE
#define BORDER_OFF  0xB8B0A0   // 节点缺席
#define BORDER_OK   0x5FB86A   // 节点在位、读到数据
#define KNOB_COL    0xFB8B24   // 编码器指针(平时)
#define PRESS_COL   0x5FB86A   // 按下(编码器指针/摇杆光点)
#define JOY_COL     0x4FB0D8   // 摇杆光点(居中)
#define JOY_MOVE    0x2E86A8   // 摇杆光点(偏移中)
#define HINT_CARD   0xFFFFFF

// 布局(每半屏一个透明容器 160×240)
#define ENC_CX      80
#define ENC_CY      132
#define ENC_R       46
#define ENC_KNOB_R  9
#define JOY_CX      80
#define JOY_CY      120
#define JOY_BOX     116
#define JOY_DOT_R   9
#define JOY_INNER   (JOY_BOX / 2 - JOY_DOT_R - 3)
#endif // CHAIN_LAB_DIAG_MODE

// ── 状态 ─────────────────────────────────────────────────────────────
static uint8_t s_enc_id;             // 0 = 未绑定;否则链上位置
static uint8_t s_joy_id;
static int16_t s_enc_value;
static bool    s_enc_btn;
static int     s_enc_streak;
static uint8_t s_enc_led[3] = { 1, 1, 1 };   // 上次写入节点的 RGB(强制首次写)

static uint16_t s_joy_cx = 2048, s_joy_cy = 2048;   // 居中校准值
static float   s_joy_nx, s_joy_ny;
static bool    s_joy_btn;
static int     s_joy_streak;
static uint8_t s_joy_led[3] = { 1, 1, 1 };

static int     s_rescan_frames;

static core2_sleep_t       s_sleep;
static core2_sleep_stage_t s_prev_stage = CORE2_SLEEP_AWAKE;

#if CHAIN_LAB_DIAG_MODE
// ── LVGL 对象(诊断台)────────────────────────────────────────────────
static lv_obj_t *s_enc_ring, *s_enc_knob, *s_enc_val;
static lv_obj_t *s_joy_boxo, *s_joy_dot, *s_joy_raw;
static lv_obj_t *s_status_lbl, *s_hint_card;

// ── 小工具 ───────────────────────────────────────────────────────────
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

// 只描边、不填充(圆环 / 方框)
static lv_obj_t *outline(lv_obj_t *parent, int w, int h, uint32_t border, int bw, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(o, bw, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const char *txt, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}
#endif // CHAIN_LAB_DIAG_MODE

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// 色相环 0..359 → RGB(饱和/亮度满,应用侧再由节点亮度档缩放)
static void hue2rgb(int h, uint8_t *r, uint8_t *g, uint8_t *b)
{
    h = ((h % 360) + 360) % 360;
    int x = 255 - abs((h % 120) * 255 / 60 - 255);
    if (x < 0) x = 0;
    if      (h < 60)  { *r = 255; *g = x;   *b = 0;   }
    else if (h < 120) { *r = x;   *g = 255; *b = 0;   }
    else if (h < 180) { *r = 0;   *g = 255; *b = x;   }
    else if (h < 240) { *r = 0;   *g = x;   *b = 255; }
    else if (h < 300) { *r = x;   *g = 0;   *b = 255; }
    else              { *r = 255; *g = 0;   *b = x;   }
}

#if CHAIN_LAB_DIAG_MODE
// ── UI 搭建(诊断台)──────────────────────────────────────────────────
static void ui_create(void)
{
    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(BG_COL), 0);

    label(scr, "Chain Lab", TEXT_COL);   // 左上标题(默认 align = 左上)

    // 左:编码器容器
    lv_obj_t *ec = plain(scr, 158, 218, PANEL_COL, 12);
    lv_obj_align(ec, LV_ALIGN_TOP_LEFT, 2, 20);
    lv_obj_t *et = label(ec, "ENCODER", TEXT_COL);
    lv_obj_align(et, LV_ALIGN_TOP_MID, 0, 6);
    s_enc_ring = outline(ec, ENC_R * 2, ENC_R * 2, BORDER_OFF, 5, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(s_enc_ring, ENC_CX - ENC_R, ENC_CY - ENC_R);
    s_enc_knob = plain(ec, ENC_KNOB_R * 2, ENC_KNOB_R * 2, KNOB_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(s_enc_knob, ENC_CX - ENC_KNOB_R, ENC_CY - ENC_R - ENC_KNOB_R);  // 12 点方向
    s_enc_val = label(ec, "--", TEXT_COL);
    lv_obj_align(s_enc_val, LV_ALIGN_TOP_MID, 0, ENC_CY - 8);

    // 右:摇杆容器
    lv_obj_t *jc = plain(scr, 158, 218, PANEL_COL, 12);
    lv_obj_align(jc, LV_ALIGN_TOP_RIGHT, -2, 20);
    lv_obj_t *jt = label(jc, "JOYSTICK", TEXT_COL);
    lv_obj_align(jt, LV_ALIGN_TOP_MID, 0, 6);
    s_joy_boxo = outline(jc, JOY_BOX, JOY_BOX, BORDER_OFF, 5, 10);
    lv_obj_set_pos(s_joy_boxo, JOY_CX - JOY_BOX / 2, JOY_CY - JOY_BOX / 2);
    s_joy_dot = plain(jc, JOY_DOT_R * 2, JOY_DOT_R * 2, JOY_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(s_joy_dot, JOY_CX - JOY_DOT_R, JOY_CY - JOY_DOT_R);
    s_joy_raw = label(jc, "X:--- Y:---", TEXT_COL);
    lv_obj_align(s_joy_raw, LV_ALIGN_BOTTOM_MID, 0, -8);

    // 底部状态行 + 无字提示卡
    s_status_lbl = label(scr, "scanning PORT.C...", TEXT_COL);
    lv_obj_align(s_status_lbl, LV_ALIGN_BOTTOM_MID, 0, -3);

    s_hint_card = plain(scr, 240, 84, HINT_CARD, 14);
    lv_obj_align(s_hint_card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *hl = label(s_hint_card, "Plug Chain unit\ninto PORT.C (blue)\nvia Chain Bridge", TEXT_COL);
    lv_obj_set_style_text_align(hl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(hl);
    lv_obj_add_flag(s_hint_card, LV_OBJ_FLAG_HIDDEN);

    bsp_display_unlock();
}

// ── UI 更新 ──────────────────────────────────────────────────────────
static void ui_enc(bool ok)
{
    bsp_display_lock(0);
    lv_obj_set_style_border_color(s_enc_ring, lv_color_hex(s_enc_id ? BORDER_OK : BORDER_OFF), 0);
    if (s_enc_id) {
        float a = (s_enc_value * ENC_DEG_PER_STEP - 90) * (float)M_PI / 180.0f;  // -90 = 12 点起
        int x = ENC_CX + (int)(ENC_R * cosf(a));
        int y = ENC_CY + (int)(ENC_R * sinf(a));
        lv_obj_set_pos(s_enc_knob, x - ENC_KNOB_R, y - ENC_KNOB_R);
        lv_obj_set_style_bg_color(s_enc_knob, lv_color_hex(s_enc_btn ? PRESS_COL : KNOB_COL), 0);
        lv_label_set_text_fmt(s_enc_val, "%d", s_enc_value);
    } else {
        lv_label_set_text(s_enc_val, "--");
    }
    (void)ok;
    bsp_display_unlock();
}

static void ui_joy(void)
{
    bsp_display_lock(0);
    lv_obj_set_style_border_color(s_joy_boxo, lv_color_hex(s_joy_id ? BORDER_OK : BORDER_OFF), 0);
    if (s_joy_id) {
        int x = JOY_CX + (int)(s_joy_nx * JOY_INNER);
        int y = JOY_CY + (int)(s_joy_ny * JOY_INNER);
        lv_obj_set_pos(s_joy_dot, x - JOY_DOT_R, y - JOY_DOT_R);
        bool moved = fabsf(s_joy_nx) > JOY_DEADZONE || fabsf(s_joy_ny) > JOY_DEADZONE;
        uint32_t c = s_joy_btn ? PRESS_COL : (moved ? JOY_MOVE : JOY_COL);
        lv_obj_set_style_bg_color(s_joy_dot, lv_color_hex(c), 0);
    }
    bsp_display_unlock();
}

static void ui_status(void)
{
    bsp_display_lock(0);
    if (!s_enc_id && !s_joy_id) {
        lv_label_set_text(s_status_lbl, "no Chain unit on PORT.C");
        lv_obj_remove_flag(s_hint_card, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text_fmt(s_status_lbl, "ENC id%d %s   JOY id%d %s",
                              s_enc_id, s_enc_id ? "OK" : "--",
                              s_joy_id, s_joy_id ? "OK" : "--");
        lv_obj_add_flag(s_hint_card, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}
#endif // CHAIN_LAB_DIAG_MODE

// ── 节点 RGB(只在变化时写,省事务)────────────────────────────────────
static void node_rgb(uint8_t id, uint8_t r, uint8_t g, uint8_t b, uint8_t last[3])
{
    if (r == last[0] && g == last[1] && b == last[2]) return;
    if (chain_bus_set_rgb(id, 0, r, g, b, 40) == ESP_OK) {
        last[0] = r; last[1] = g; last[2] = b;
    }
}

// ── 探测 / 绑定 ───────────────────────────────────────────────────────
static void joy_calibrate_center(uint8_t id)
{
    uint32_t sx = 0, sy = 0; int ok = 0;
    for (int i = 0; i < 6; i++) {
        uint16_t x, y;
        if (unit_chain_joystick_read_adc(id, &x, &y) == ESP_OK) { sx += x; sy += y; ok++; }
        vTaskDelay(pdMS_TO_TICKS(8));
    }
    if (ok) { s_joy_cx = sx / ok; s_joy_cy = sy / ok; }
    else    { s_joy_cx = 2048;    s_joy_cy = 2048;    }
    ESP_LOGI(TAG, "摇杆居中校准:center=(%u,%u)", s_joy_cx, s_joy_cy);
}

// 扫描链上 1..CHAIN_MAX_ID,认领第一颗 encoder / joystick(只扫尚未绑定的槽)
static void scan_bus(bool greet)
{
    for (uint8_t id = 1; id <= CHAIN_MAX_ID; id++) {
        if ((s_enc_id == id) || (s_joy_id == id)) continue;
        chain_dev_type_t type;
        if (chain_bus_get_device_type(id, &type, 60) != ESP_OK) continue;
        ESP_LOGI(TAG, "链位 %u:设备类型 0x%04X", id, type);
        if (type == CHAIN_DEV_ENCODER && !s_enc_id) {
            s_enc_id = id; s_enc_streak = 0; s_enc_led[0] = 1;
            chain_bus_set_rgb_brightness(id, NODE_RGB_BRIGHTNESS, 40);
            ESP_LOGI(TAG, "Chain Encoder 接管 @链位%u", id);
        } else if (type == CHAIN_DEV_JOYSTICK && !s_joy_id) {
            s_joy_id = id; s_joy_streak = 0; s_joy_led[0] = 1;
            chain_bus_set_rgb_brightness(id, NODE_RGB_BRIGHTNESS, 40);
            joy_calibrate_center(id);
            ESP_LOGI(TAG, "Chain Joystick 接管 @链位%u", id);
        }
    }

    if (!s_enc_id && !s_joy_id) {
        ESP_LOGW(TAG, "PORT.C 上没认到 Chain 节点。排查:①插蓝口 PORT.C(不是红 A/黑 B)"
                      "②Chain Bridge 箭头朝主控(IN 朝 Core2)③节点 5V(底座灯带亮=5V 有电)");
        chain_bus_sniff(300);   // 抓原始字节:有心跳=链路通只是没认到;全无=没供电/接反/直连不成立
    } else if (greet) {
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
    }
}

// ── 轮询(draw=false 时只读取以侦测活动,不动屏/不写节点灯)────────────
static bool poll_enc(bool draw)
{
    if (!s_enc_id) return false;
    int16_t v; bool btn = s_enc_btn;
    if (unit_chain_encoder_read_value(s_enc_id, &v) != ESP_OK) {
        if (++s_enc_streak >= ERR_STREAK_LOST) {
            ESP_LOGW(TAG, "Encoder 失联(拔线/断电?)"); s_enc_id = 0;
        }
        return false;
    }
    s_enc_streak = 0;
    unit_chain_encoder_read_button(s_enc_id, &btn);   // 尽力读,失败保留旧值

    bool press_edge = btn && !s_enc_btn;              // 相对上一帧的上升沿
    bool activity   = (v != s_enc_value) || btn;      // 转动或按住 = 有人玩
    s_enc_value = v; s_enc_btn = btn;

#if CHAIN_LAB_DIAG_MODE
    if (draw) {
        ui_enc(true);
        if (btn) node_rgb(s_enc_id, 255, 255, 255, s_enc_led);   // 按下 = 白
        else {
            uint8_t r, g, b; hue2rgb(((s_enc_value % 24) + 24) % 24 * 15, &r, &g, &b);
            node_rgb(s_enc_id, r, g, b, s_enc_led);
        }
        if (press_edge) { audio_fx_play(SND_COLLECT); haptics_play(HAPTIC_COLLECT); }
    }
#else
    (void)draw; (void)press_edge;
#endif
    return activity;
}

static bool poll_joy(bool draw)
{
    if (!s_joy_id) return false;
    uint16_t x, y; bool btn = s_joy_btn;
    if (unit_chain_joystick_read_adc(s_joy_id, &x, &y) != ESP_OK) {
        if (++s_joy_streak >= ERR_STREAK_LOST) {
            ESP_LOGW(TAG, "Joystick 失联(拔线/断电?)"); s_joy_id = 0;
        }
        return false;
    }
    s_joy_streak = 0;
    unit_chain_joystick_read_button(s_joy_id, &btn);

    float rx = ((float)x - s_joy_cx) / JOY_HALF_SPAN;
    float ry = ((float)y - s_joy_cy) / JOY_HALF_SPAN;
#if JOY_SWAP_XY
    float tmp = rx; rx = ry; ry = tmp;
#endif
#if JOY_INVERT_X
    rx = -rx;
#endif
#if JOY_INVERT_Y
    ry = -ry;
#endif
    float nx = clampf(rx, -1.0f, 1.0f);
    float ny = clampf(ry, -1.0f, 1.0f);
    bool moved = fabsf(nx) > JOY_MOVE_KICK || fabsf(ny) > JOY_MOVE_KICK;
    bool press_edge = btn && !s_joy_btn;             // 相对上一帧的上升沿
    bool activity   = moved || btn;                  // 偏移或按住 = 有人玩
    s_joy_nx = nx; s_joy_ny = ny;
    s_joy_btn = btn;

#if CHAIN_LAB_DIAG_MODE
    if (draw) {
        bsp_display_lock(0);
        lv_label_set_text_fmt(s_joy_raw, "X:%4u Y:%4u", x, y);
        bsp_display_unlock();
        ui_joy();
        if (btn) node_rgb(s_joy_id, 255, 255, 255, s_joy_led);
        else {
            uint8_t r = (uint8_t)(clampf((nx + 1) * 0.5f, 0, 1) * 255);
            uint8_t b = (uint8_t)(clampf((ny + 1) * 0.5f, 0, 1) * 255);
            node_rgb(s_joy_id, r, 60, b, s_joy_led);
        }
        if (press_edge) { audio_fx_play(SND_COLLECT); haptics_play(HAPTIC_COLLECT); }
    }
#else
    (void)draw; (void)press_edge;
#endif
    return activity;
}

// ── 主任务(20Hz)─────────────────────────────────────────────────────
static void game_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        imu_accel_t acc;
        bool have = (imu_mpu6886_read_accel(&acc) == ESP_OK);
        int delay_ms = core2_sleep_feed(&s_sleep,
                                        have ? (float[]){ acc.x, acc.y, acc.z } : NULL, true);
        core2_sleep_stage_t stage = core2_sleep_stage(&s_sleep);

        // 深度省电切过 M-Bus 5V → Chain 节点掉电复位:醒来重扫
        if (s_prev_stage == CORE2_SLEEP_DEEP && stage != CORE2_SLEEP_DEEP) {
            s_enc_id = 0; s_joy_id = 0;
            scan_bus(false);
#if CHAIN_LAB_DIAG_MODE
            ui_status();
#else
            crane_game_sync_attach();
            crane_game_reset_position();   // 爪子/吊臂复位安全位置(SPEC §6)
#endif
        }
        s_prev_stage = stage;

        if (stage != CORE2_SLEEP_DEEP) {
#if CHAIN_LAB_DIAG_MODE
            bool draw = (stage == CORE2_SLEEP_AWAKE);
            bool prev_bound = s_enc_id || s_joy_id;

            if (!s_enc_id || !s_joy_id) {   // 有空位就低频重扫(认新插入 / 恢复失联)
                if (++s_rescan_frames >= ATTACH_RETRY_MS / POLL_PERIOD_MS) {
                    s_rescan_frames = 0;
                    scan_bus(true);
                    if (draw) ui_status();
                }
            }

            bool act = poll_enc(draw);
            act |= poll_joy(draw);

            if ((s_enc_id || s_joy_id) != prev_bound && draw) ui_status();

            if (act) {
                core2_sleep_kick(&s_sleep);
                if (stage != CORE2_SLEEP_AWAKE) core2_sleep_wake(&s_sleep);
            }
#else
            bool prev_bound = s_enc_id || s_joy_id;

            if (!s_enc_id || !s_joy_id) {   // 有空位就低频重扫(认新插入 / 恢复失联)
                if (++s_rescan_frames >= ATTACH_RETRY_MS / POLL_PERIOD_MS) {
                    s_rescan_frames = 0;
                    scan_bus(true);
                    crane_game_sync_attach();
                }
            }

            bool act = poll_enc(false);   // 只读值/测活动,不落诊断台画面(游戏层自己画)
            act |= poll_joy(false);

            if ((s_enc_id || s_joy_id) != prev_bound) crane_game_sync_attach();

            if (act) {
                core2_sleep_kick(&s_sleep);
                if (stage != CORE2_SLEEP_AWAKE) core2_sleep_wake(&s_sleep);
            }

            if (stage == CORE2_SLEEP_AWAKE) crane_game_tick();
#endif
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms));
    }
}

void chain_lab_start(void)
{
#if CHAIN_LAB_DIAG_MODE
    ui_create();
#else
    crane_game_create();
#endif

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

#if CHAIN_LAB_DIAG_MODE
    ui_status();
#else
    crane_game_sync_attach();
#endif

    if (s_enc_id || s_joy_id) {
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
    } else {
        audio_fx_play(SND_BUMP_MED);   // 没认到:温柔一声"咦?",屏上出提示卡
    }

    xTaskCreate(game_task, "chain_lab", 5120, NULL, 5, NULL);
}

// ── 游戏层输入 getter(SPEC §2:应用侧自算帧间 delta/边沿,这里只暴露读数)────────
bool    chain_lab_enc_attached(void) { return s_enc_id != 0; }
int16_t chain_lab_enc_value(void)    { return s_enc_value; }
bool    chain_lab_enc_button(void)   { return s_enc_btn; }

bool    chain_lab_joy_attached(void) { return s_joy_id != 0; }
float   chain_lab_joy_x(void)        { return s_joy_nx; }
float   chain_lab_joy_y(void)        { return s_joy_ny; }
bool    chain_lab_joy_button(void)   { return s_joy_btn; }

void chain_lab_enc_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_enc_id) return;
    node_rgb(s_enc_id, r, g, b, s_enc_led);
}

void chain_lab_joy_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_joy_id) return;
    node_rgb(s_joy_id, r, g, b, s_joy_led);
}

void chain_lab_hue2rgb(int h, uint8_t *r, uint8_t *g, uint8_t *b) { hue2rgb(h, r, g, b); }
