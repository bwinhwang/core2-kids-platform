// launcher —— 幼儿游戏机选择页(factory 分区常驻)
//
// 职责:上电展示"游戏卡带架"(6 个 ota 槽的大图标),点击 → app_slot_launch 重启进游戏;
//       游戏内电源键短按 / 任何复位都会回到这里(机制见 components/app_slot/README.md)。
// 原则:无文字承载信息(§13)、暖色低亮、点击必有回应(空槽也给"啵"一声,零失败);
//       久置走 core2_sleep 两级省电(选择页也耗电池)。
//
// 【二期预留】PORT.A 探测(DLight 0x23/8Encoder 0x41/超声波 0x57/手势 0x73):
//   探到单元 → 直接进对应游戏槽,"插卡带即开机";实现放 probe.c,勿混进本文件。

#include <string.h>

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

static const char *TAG = "launcher";

static core2_sleep_t s_sleep;
static volatile int  s_pending = -1;  // 待启动槽位(点击回调置位,主循环消费;-1=无)

// 槽位马卡龙色(依次:草绿/海蓝/星紫/糖粉/蜜黄/薄荷,§18.2 色系)
static const uint32_t SLOT_COLORS[APP_SLOT_COUNT] = {
    0xA7C957, 0x4FB0D8, 0x9C9AD0, 0xFF8FB0, 0xFFC75F, 0x7FD0C0,
};

// ── 小工具:无样式裸对象(纯色块拼图形用)─────────────────────────────
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

// ── 吉祥物「圆圆」小脸(顶部招牌,轻微上下浮动)───────────────────────
static void mascot_bob_cb(void *obj, int32_t v) { lv_obj_set_y((lv_obj_t *)obj, v); }

static void make_mascot(lv_obj_t *scr)
{
    lv_obj_t *face = plain(scr, 44, 44, 0xFFD23F, LV_RADIUS_CIRCLE);  // 身体
    lv_obj_set_pos(face, (320 - 44) / 2, 10);

    lv_obj_t *el = plain(face, 9, 9, 0xFFFFFF, LV_RADIUS_CIRCLE);     // 眼白 ×2
    lv_obj_align(el, LV_ALIGN_CENTER, -9, -4);
    lv_obj_t *er = plain(face, 9, 9, 0xFFFFFF, LV_RADIUS_CIRCLE);
    lv_obj_align(er, LV_ALIGN_CENTER, 9, -4);
    lv_obj_t *pl = plain(el, 4, 4, 0x3A3A38, LV_RADIUS_CIRCLE);       // 瞳孔
    lv_obj_center(pl);
    lv_obj_t *pr = plain(er, 4, 4, 0x3A3A38, LV_RADIUS_CIRCLE);
    lv_obj_center(pr);
    lv_obj_t *beak = plain(face, 8, 6, 0xFB8B24, 3);                  // 小喙
    lv_obj_align(beak, LV_ALIGN_CENTER, 0, 8);

    // 上下浮动 ±4px(局部小区域低频动效,§9.5 帧预算内)
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, face);
    lv_anim_set_exec_cb(&a, mascot_bob_cb);
    lv_anim_set_values(&a, 8, 16);
    lv_anim_set_duration(&a, 1400);
    lv_anim_set_playback_duration(&a, 1400);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

// ── 图标:倾斜迷宫(白底小迷宫 + 两道墙 + 球 + 家)────────────────────
static void make_maze_icon(lv_obj_t *btn)
{
    lv_obj_t *panel = plain(btn, 44, 40, 0xFFFFFF, 8);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *w1 = plain(panel, 26, 6, 0x7FB069, 3);   // 墙(树篱绿)
    lv_obj_set_pos(w1, 0, 10);
    lv_obj_t *w2 = plain(panel, 26, 6, 0x7FB069, 3);
    lv_obj_align(w2, LV_ALIGN_TOP_RIGHT, 0, 22);
    lv_obj_t *ball = plain(panel, 10, 10, 0xFFD23F, LV_RADIUS_CIRCLE);  // 圆圆
    lv_obj_set_pos(ball, 4, 26);
    lv_obj_t *home = plain(panel, 10, 10, 0xC68A52, LV_RADIUS_CIRCLE);  // 家(鸟窝)
    lv_obj_align(home, LV_ALIGN_TOP_RIGHT, -4, 2);
}

// ── 图标:通用游戏(白色笑脸占位;新游戏可在此加专属图标分支)──────────
static void make_generic_icon(lv_obj_t *btn)
{
    lv_obj_t *face = plain(btn, 38, 38, 0xFFFFFF, LV_RADIUS_CIRCLE);
    lv_obj_align(face, LV_ALIGN_TOP_MID, 0, 9);
    lv_obj_t *el = plain(face, 7, 7, 0x3A3A38, LV_RADIUS_CIRCLE);
    lv_obj_align(el, LV_ALIGN_CENTER, -8, -2);
    lv_obj_t *er = plain(face, 7, 7, 0x3A3A38, LV_RADIUS_CIRCLE);
    lv_obj_align(er, LV_ALIGN_CENTER, 8, -2);
    lv_obj_t *mouth = plain(face, 12, 5, 0xFB8B24, 2);
    lv_obj_align(mouth, LV_ALIGN_CENTER, 0, 9);
}

// ── 点击:空槽也回应(零失败),有效槽记下待启动 ──────────────────────
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

// ── 一个游戏槽按钮(96×80;有游戏=彩色+图标,空槽=灰色凹槽)────────────
static void make_slot(lv_obj_t *scr, int idx, int x, int y)
{
    char name[32] = "";
    bool present = app_slot_present(idx, name, sizeof(name));

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, 96, 80);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    if (present) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(SLOT_COLORS[idx]), 0);
        lv_obj_add_event_cb(btn, on_slot_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
        if (strcmp(name, "tilt_maze") == 0) make_maze_icon(btn);
        else                                make_generic_icon(btn);
        // 小字工程名:给家长/调试认卡带用,幼儿靠颜色+图标(文字仅装饰,§13)
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, name);
        lv_obj_set_width(lbl, 88);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x3A3A38), 0);
        lv_obj_set_style_text_opa(lbl, LV_OPA_70, 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, 2);
        ESP_LOGI(TAG, "槽 ota_%d: %s", idx, name);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xE4DFD2), 0);
        lv_obj_add_event_cb(btn, on_slot_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
        lv_obj_t *hole = plain(btn, 30, 30, 0xD2CCBC, LV_RADIUS_CIRCLE);  // 空卡槽凹窝
        lv_obj_center(hole);
    }
}

static void ui_create(void)
{
    bsp_display_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF6EED9), 0);  // 暖米色(压亮度,§13)
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    make_mascot(scr);

    // 3×2 卡带架:x=8/112/216,y=64/152(96×80 + 8px 间距,铺满 320×240)
    for (int i = 0; i < APP_SLOT_COUNT; i++) {
        make_slot(scr, i, 8 + (i % 3) * 104, 64 + (i / 3) * 88);
    }

    bsp_display_unlock();
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== 幼儿游戏机 launcher 启动 ===");

    // ① 平台一键 bring-up(顺序知识在 core2_board,勿散装重写)
    core2_board_cfg_t cfg = CORE2_BOARD_CFG_KIDS_DEFAULT;   // 全开、低亮(60%/灯带≤48)
    esp_err_t err = core2_board_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "平台初始化失败(%s)%s", esp_err_to_name(err),
                 err == ESP_ERR_NOT_FOUND ? ":确认 Bottom2 底座在位(IMU/电池都来自底座)" : "");
        return;
    }

    // ② 选择页 UI + 问候
    ui_create();
    audio_fx_play(SND_HELLO);
    haptics_play(HAPTIC_HELLO);

    // ③ 主循环:消费"待启动"槽位 + 喂省电编排(选择页久置 → 打盹/深度省电)
    core2_sleep_init(&s_sleep, NULL);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        if (s_pending >= 0) {
            int idx = s_pending;
            vTaskDelay(pdMS_TO_TICKS(250));      // 让点击音效播出去
            err = app_slot_launch(idx);          // 成功不返回(重启进游戏)
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
        vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms));
    }
}
