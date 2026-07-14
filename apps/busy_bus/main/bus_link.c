// busy_bus —— 传输/绑定层实现(形状照抄 chain_lab.c 的 scan_bus/poll_joy/node_rgb/
// hue2rgb,收窄为 joystick-only;encoder 若挂在链上会被跳过认领,不冲突,SPEC §1)。
//
// 供电/接线/省电坑同 chain_bus 头注释(PORT.C 5V=EXTEN;深度省电切 5V→节点复位→重扫)。
#include "bus_link.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "audio_fx.h"
#include "core2_sleep.h"
#include "haptics.h"
#include "imu_mpu6886.h"
#include "ledstrip_fx.h"

#include "chain_bus.h"
#include "unit_chain_joystick.h"

#include "bus_game.h"

#include "tuning.h"

static const char *TAG = "bus_link";

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

// ── 状态 ─────────────────────────────────────────────────────────────
static uint8_t s_joy_id;             // 0 = 未绑定;否则链上位置
static float   s_joy_cx = 2048.f, s_joy_cy = 2048.f;   // 居中校准值(持续自适应回中,见 poll_joy)
static float   s_joy_nx, s_joy_ny;
static bool    s_joy_btn;
static int     s_joy_streak;
static uint8_t s_joy_led[3] = { 1, 1, 1 };   // 上次写入节点的 RGB(强制首次写)

static int     s_rescan_frames;

static core2_sleep_t       s_sleep;
static core2_sleep_stage_t s_prev_stage = CORE2_SLEEP_AWAKE;

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
    if (ok) { s_joy_cx = (float)sx / ok; s_joy_cy = (float)sy / ok; }
    else    { s_joy_cx = 2048.f;         s_joy_cy = 2048.f;         }
    ESP_LOGI(TAG, "摇杆居中校准:center=(%.0f,%.0f)", s_joy_cx, s_joy_cy);
}

// 扫描链上 1..CHAIN_MAX_ID,认领第一颗 joystick(encoder 若挂着也会被扫到但直接跳过)
static void scan_bus(bool greet)
{
    for (uint8_t id = 1; id <= CHAIN_MAX_ID; id++) {
        if (s_joy_id == id) continue;
        chain_dev_type_t type;
        if (chain_bus_get_device_type(id, &type, 60) != ESP_OK) continue;
        ESP_LOGI(TAG, "链位 %u:设备类型 0x%04X", id, type);
        if (type == CHAIN_DEV_JOYSTICK && !s_joy_id) {
            s_joy_id = id; s_joy_streak = 0; s_joy_led[0] = 1;
            chain_bus_set_rgb_brightness(id, NODE_RGB_BRIGHTNESS, 40);
            joy_calibrate_center(id);
            ESP_LOGI(TAG, "Chain Joystick 接管 @链位%u", id);
        }
    }

    if (!s_joy_id) {
        ESP_LOGW(TAG, "PORT.C 上没认到 Chain Joystick。排查:①插蓝口 PORT.C(不是红 A/黑 B)"
                      "②Chain Bridge 箭头朝主控(IN 朝 Core2)③节点 5V(底座灯带亮=5V 有电)");
        chain_bus_sniff(300);   // 抓原始字节:有心跳=链路通只是没认到;全无=没供电/接反/直连不成立
    } else if (greet) {
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
    }
}

// ── 轮询(draw=false:只读取以侦测活动,游戏层自己画)────────────────────
static bool poll_joy(bool draw)
{
    (void)draw;
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

    // 自适应回中:实测这颗摇杆松手后的静止点会偏离开机校准中心(偏移可达 ~0.25,
    // 超过死区),机械回中误差/温漂,固定死区盖不住。只在偏移还没到"明显在推"的
    // 回中带内才每帧把中心慢慢拉过去;超出回中带(真的在推)时那一路轴暂停校正,
    // 避免长按摇杆开车被拉成新中心、按住反而停下来。
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
    float nx = clampf(rx, -1.0f, 1.0f);
    float ny = clampf(ry, -1.0f, 1.0f);
    bool moved = fabsf(nx) > JOY_MOVE_KICK || fabsf(ny) > JOY_MOVE_KICK;
    bool activity = moved || btn;   // 偏移或按住 = 有人玩

    s_joy_nx = nx; s_joy_ny = ny;
    s_joy_btn = btn;
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
            s_joy_id = 0;
            scan_bus(false);
            bus_game_sync_attach();
            bus_game_reset_position();   // 巴士复位安全位置(车库),不吞送达进度(SPEC §10)
        }
        s_prev_stage = stage;

        if (stage != CORE2_SLEEP_DEEP) {
            bool prev_bound = s_joy_id != 0;

            if (!s_joy_id) {   // 没绑定就低频重扫(认新插入 / 恢复失联)
                if (++s_rescan_frames >= ATTACH_RETRY_MS / POLL_PERIOD_MS) {
                    s_rescan_frames = 0;
                    scan_bus(true);
                    bus_game_sync_attach();
                }
            }

            bool act = poll_joy(false);

            if ((s_joy_id != 0) != prev_bound) bus_game_sync_attach();

            if (act) {
                core2_sleep_kick(&s_sleep);
                if (stage != CORE2_SLEEP_AWAKE) core2_sleep_wake(&s_sleep);
            }

            if (stage == CORE2_SLEEP_AWAKE) bus_game_tick();
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms));
    }
}

void bus_link_start(void)
{
    bus_game_create();

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

    bus_game_sync_attach();

    if (s_joy_id) {
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
    } else {
        audio_fx_play(SND_BUMP_MED);   // 没认到:温柔一声"咦?",屏上出提示卡
    }

    xTaskCreate(game_task, "bus_link", 5120, NULL, 5, NULL);
}

// ── 游戏层输入 getter(应用侧自算帧间 delta/边沿,这里只暴露读数)────────────
bool  bus_link_joy_attached(void) { return s_joy_id != 0; }
float bus_link_joy_x(void)        { return s_joy_nx; }
float bus_link_joy_y(void)        { return s_joy_ny; }
bool  bus_link_joy_button(void)   { return s_joy_btn; }

void bus_link_joy_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_joy_id) return;
    node_rgb(s_joy_id, r, g, b, s_joy_led);
}

void bus_link_hue2rgb(int h, uint8_t *r, uint8_t *g, uint8_t *b) { hue2rgb(h, r, g, b); }
