#include "parent_menu.h"
#include "tuning.h"

#include "esp_log.h"
#include "bsp/m5stack_core_2.h"
#include "lvgl.h"

#include "maze_audio.h"
#include "haptics.h"
#include "game_state.h"

static const char *TAG = "parent";

static lv_obj_t *s_menu;          // 菜单根容器(NULL=未打开)
static lv_obj_t *s_vib_lbl;
static lv_obj_t *s_band_lbl;

// 当前设置(每次开机默认值;持久化到 NVS 可后续加)
static int  s_bright = PLAY_BRIGHTNESS;
static int  s_volume = 60;
static bool s_vib    = true;
static bool s_easy   = false;     // 难度档:false=普通(4 关) true=简单(2 关)

static void close_menu(void)
{
    if (!s_menu) return;
    lv_obj_delete(s_menu);
    s_menu = NULL;
    game_state_set_paused(false);
}

// ── 控件回调(均在 LVGL 任务上下文,勿再 bsp_display_lock)──────────────
static void on_bright(lv_event_t *e)
{
    int v = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
    s_bright = v;
    game_state_set_play_brightness(v);   // 立即应用 + 记为常态亮度
}

static void on_volume(lv_event_t *e)
{
    int v = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
    s_volume = v;
    maze_audio_set_volume(v);
}

static void on_vib(lv_event_t *e)
{
    s_vib = !s_vib;
    haptics_set_enabled(s_vib);
    lv_label_set_text(s_vib_lbl, s_vib ? "Vibration: ON" : "Vibration: OFF");
}

static void on_band(lv_event_t *e)
{
    s_easy = !s_easy;
    game_state_set_level_band(s_easy ? 2 : 4);
    lv_label_set_text(s_band_lbl, s_easy ? "Levels: 2 (Easy)" : "Levels: 4 (Normal)");
}

static void on_recal(lv_event_t *e)
{
    close_menu();
    game_state_request_recalibrate();
}

static void on_back(lv_event_t *e) { close_menu(); }

// ── 小工具:做一个带文字的按钮 ───────────────────────────────────────
static lv_obj_t *make_btn(lv_obj_t *parent, int x, int y, int w, int h,
                          const char *text, lv_event_cb_t cb, lv_obj_t **out_lbl)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    if (out_lbl) *out_lbl = lbl;
    return btn;
}

static void open_menu(void)
{
    if (s_menu) return;
    game_state_set_paused(true);

    // 全屏半暗背板(吃掉所有触摸),建在 top layer → 盖住游戏
    s_menu = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_menu);
    lv_obj_set_size(s_menu, (int)PLAY_W, (int)PLAY_H);
    lv_obj_set_pos(s_menu, 0, 0);
    lv_obj_set_style_bg_color(s_menu, lv_color_hex(0x202830), 0);
    lv_obj_set_style_bg_opa(s_menu, LV_OPA_80, 0);
    lv_obj_remove_flag(s_menu, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_menu);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " PARENT");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(title, 12, 8);

    // 亮度
    lv_obj_t *bl = lv_label_create(s_menu);
    lv_label_set_text(bl, "Bright");
    lv_obj_set_style_text_color(bl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(bl, 12, 40);
    lv_obj_t *bs = lv_slider_create(s_menu);
    lv_obj_set_pos(bs, 110, 42);
    lv_obj_set_size(bs, 180, 14);
    lv_slider_set_range(bs, 5, 100);
    lv_slider_set_value(bs, s_bright, LV_ANIM_OFF);
    lv_obj_add_event_cb(bs, on_bright, LV_EVENT_VALUE_CHANGED, NULL);

    // 音量
    lv_obj_t *vl = lv_label_create(s_menu);
    lv_label_set_text(vl, "Volume");
    lv_obj_set_style_text_color(vl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(vl, 12, 72);
    lv_obj_t *vs = lv_slider_create(s_menu);
    lv_obj_set_pos(vs, 110, 74);
    lv_obj_set_size(vs, 180, 14);
    lv_slider_set_range(vs, 0, 100);
    lv_slider_set_value(vs, s_volume, LV_ANIM_OFF);
    lv_obj_add_event_cb(vs, on_volume, LV_EVENT_VALUE_CHANGED, NULL);

    // 震动 / 难度
    make_btn(s_menu, 12, 104, 140, 34, s_vib ? "Vibration: ON" : "Vibration: OFF", on_vib, &s_vib_lbl);
    make_btn(s_menu, 160, 104, 148, 34, s_easy ? "Levels: 2 (Easy)" : "Levels: 4 (Normal)", on_band, &s_band_lbl);

    // 重新校准 / 返回
    make_btn(s_menu, 12, 148, 140, 42, LV_SYMBOL_REFRESH " Recal", on_recal, NULL);
    make_btn(s_menu, 160, 148, 148, 42, LV_SYMBOL_CLOSE " Back", on_back, NULL);

    ESP_LOGI(TAG, "家长菜单已打开(游戏暂停)");
}

static void on_longpress(lv_event_t *e) { open_menu(); }

void parent_menu_init(void)
{
    bsp_display_lock(0);

    // 长按时间设为 3s,防幼儿误触
    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev) lv_indev_set_long_press_time(indev, PARENT_LONGPRESS_MS);

    // 底部透明长按热区(top layer,覆盖底部一条),不挡视觉
    lv_obj_t *hot = lv_button_create(lv_layer_top());
    lv_obj_remove_style_all(hot);
    lv_obj_set_size(hot, (int)PLAY_W, 48);
    lv_obj_set_pos(hot, 0, (int)PLAY_H - 48);
    lv_obj_set_style_bg_opa(hot, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(hot, on_longpress, LV_EVENT_LONG_PRESSED, NULL);

    bsp_display_unlock();
    ESP_LOGI(TAG, "家长菜单入口就绪(底部长按 %dms)", PARENT_LONGPRESS_MS);
}
