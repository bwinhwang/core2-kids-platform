// launcher —— IoT 评估台选择页(factory 分区常驻)
//
// 职责:上电展示"评估卡带架"(6 个 ota 槽的数据驱动卡片),点击 → app_slot_launch 重启进
//       评估 app;评估 app 内任何复位都会回到这里(机制见 components/app_slot/README.md)。
//
// 2026-07-17 平台转向重写(取代旧版幼儿掌机选择页):删 mascot/7 个手绘图标函数/strcmp
// 分支/马卡龙色表,槽卡片改**数据驱动渲染**——直读 app_slot_info() 的 project_name/
// version/date,不再需要为每个新 app 手绘图标 + 重刷 launcher(CLAUDE.md §10)。
// 顶部加 ui_status_bar(电池/USB,power_monitor 遥测),深灰工程风配色。

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/m5stack_core_2.h"
#include "lvgl.h"

#include "app_slot.h"
#include "audio_fx.h"
#include "core2_board.h"
#include "core2_sleep.h"
#include "haptics.h"
#include "imu_mpu6886.h"
#include "power_monitor.h"
#include "ui_kit.h"

static const char *TAG = "launcher";

static core2_sleep_t   s_sleep;
static ui_status_bar_t *s_status_bar;
static volatile int    s_pending = -1;  // 待启动槽位(点击回调置位,主循环消费;-1=无)
static bool             s_power_ready = false;

// 深灰工程风配色(与幼儿掌机时期马卡龙色系区分,§ui_kit 同一套色板)
#define COLOR_BG        0x1E2126   // 屏背景(比卡片再深一档)
#define COLOR_EMPTY_BG  0x24282D   // 空槽卡片背景

// ── 粗略电量映射(LiPo 3.3V=0% ~ 4.2V=100% 线性近似,精度待实机标定)────────────
static int batt_mv_to_pct(int mv)
{
    if (mv <= 3300) return 0;
    if (mv >= 4200) return 100;
    return (int)((mv - 3300) * 100 / (4200 - 3300));
}

// ── 无样式裸对象(纯色块拼图形用)───────────────────────────────────────────
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

// ── 点击:空槽也回应(轻反馈),有效槽记下待启动 ──────────────────────────────
static void on_slot_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    core2_sleep_wake(&s_sleep);            // 打盹中被点也先醒来
    if (idx < 0) {                          // 空卡槽:轻轻"啵"一声,不启动
        audio_fx_play(SND_BUMP_LIGHT);
        return;
    }
    if (s_pending >= 0) return;             // 已在启动路上,忽略连点
    audio_fx_play(SND_COLLECT);
    haptics_play(HAPTIC_COLLECT);
    s_pending = idx;                        // 主循环消费(等音效播出再重启)
}

// ── 一个评估槽卡片(96×80;有 app=深灰卡片+数据驱动文字,空槽=更暗凹槽)────────
static void make_slot(lv_obj_t *scr, int idx, int x, int y)
{
    app_slot_info_t info;
    bool present = app_slot_info(idx, &info);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_remove_style_all(btn);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, 96, 80);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

    if (present) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(UI_KIT_COLOR_PANEL), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(btn, on_slot_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

        // 工程名(数据驱动,直读 esp_app_desc_t,不再手绘图标 + strcmp 分支)
        lv_obj_t *name = lv_label_create(btn);
        lv_label_set_text(name, info.project_name);
        lv_obj_set_width(name, 88);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(UI_KIT_COLOR_VALUE), 0);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 10);

        // 版本/编译日期(小字,家长掌机时期是给家长认卡带用;评估台给评估者核对版本)
        lv_obj_t *ver = lv_label_create(btn);
        lv_label_set_text_fmt(ver, "%s %s", info.version[0] ? info.version : "-", info.date);
        lv_obj_set_width(ver, 88);
        lv_label_set_long_mode(ver, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(ver, lv_color_hex(UI_KIT_COLOR_LABEL), 0);
        lv_obj_set_style_text_align(ver, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, -8);

        // ota_N 角标(右上角)
        lv_obj_t *badge = lv_label_create(btn);
        lv_label_set_text_fmt(badge, "%d", idx);
        lv_obj_set_style_text_color(badge, lv_color_hex(UI_KIT_COLOR_ACCENT), 0);
        lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -4, 2);

        ESP_LOGI(TAG, "槽 ota_%d: %s %s", idx, info.project_name, info.version);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_EMPTY_BG), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(btn, on_slot_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)-1);

        lv_obj_t *hole = plain(btn, 24, 24, 0x33383E, LV_RADIUS_CIRCLE);
        lv_obj_align(hole, LV_ALIGN_CENTER, 0, -8);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, "empty");
        lv_obj_set_style_text_color(label, lv_color_hex(UI_KIT_COLOR_LABEL), 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -18);

        lv_obj_t *badge = lv_label_create(btn);
        lv_label_set_text_fmt(badge, "ota_%d", idx);
        lv_obj_set_style_text_color(badge, lv_color_hex(0x4A4F55), 0);
        lv_obj_align(badge, LV_ALIGN_BOTTOM_MID, 0, -4);
    }
}

static void ui_create(void)
{
    bsp_display_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_status_bar = ui_status_bar_create(scr, "launcher");

    // 3×2 评估槽卡片架:x=8/112/216,y=36/124(96×80 + 8px 间距,状态栏 24px 之下)
    for (int i = 0; i < APP_SLOT_COUNT; i++) {
        make_slot(scr, i, 8 + (i % 3) * 104, 36 + (i / 3) * 88);
    }

    bsp_display_unlock();
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== IoT 评估台 launcher 启动 ===");

    // ① 平台一键 bring-up(顺序知识在 core2_board,勿散装重写)
    core2_board_cfg_t cfg = CORE2_BOARD_CFG_DEFAULT;
    esp_err_t err = core2_board_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "平台初始化失败(%s)%s", esp_err_to_name(err),
                 err == ESP_ERR_NOT_FOUND ? ":确认 Bottom2 底座在位(IMU/电池都来自底座)" : "");
        return;
    }

    // ② AXP192 遥测(状态栏电量/USB 用;失败不拦选择页,状态栏显示"--")
    s_power_ready = (power_monitor_init() == ESP_OK);
    if (!s_power_ready) ESP_LOGW(TAG, "power_monitor 初始化失败,状态栏电量不可用");

    // ③ 选择页 UI + 问候
    ui_create();
    audio_fx_play(SND_HELLO);
    haptics_play(HAPTIC_HELLO);

    // ④ 主循环:消费"待启动"槽位 + 喂省电编排 + 1Hz 刷新状态栏
    core2_sleep_init(&s_sleep, NULL);
    TickType_t last = xTaskGetTickCount();
    int64_t last_status_update_ms = 0;
    for (;;) {
        if (s_pending >= 0) {
            int idx = s_pending;
            vTaskDelay(pdMS_TO_TICKS(250));      // 让点击音效播出去
            err = app_slot_launch(idx);          // 成功不返回(重启进评估 app)
            ESP_LOGW(TAG, "槽 ota_%d 启动失败(%s):空槽或镜像损坏,重烧该槽 bin",
                     idx, esp_err_to_name(err));
            audio_fx_play(SND_BUMP_MED);         // 温柔地"没成功"一声
            s_pending = -1;
        }

        imu_accel_t a;
        bool have = imu_mpu6886_read_accel(&a) == ESP_OK;
        int delay_ms = core2_sleep_feed(&s_sleep,
                                        have ? (float[]){ a.x, a.y, a.z } : NULL,
                                        true);

        int64_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now_ms - last_status_update_ms >= 1000) {
            last_status_update_ms = now_ms;
            int batt_pct = -1;
            bool vbus = false;
            if (s_power_ready) {
                power_telemetry_t t;
                if (power_monitor_read(&t) == ESP_OK) {
                    batt_pct = batt_mv_to_pct(t.batt_mv);
                    vbus = t.vbus_present;
                }
            }
            bsp_display_lock(0);
            ui_status_bar_update(s_status_bar, (uint32_t)(now_ms / 1000), batt_pct, vbus);
            bsp_display_unlock();
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms));
    }
}
