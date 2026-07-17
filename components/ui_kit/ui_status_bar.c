#include "ui_kit.h"

#include <stdlib.h>
#include <stdio.h>

struct ui_status_bar_s {
    lv_obj_t *uptime_label;
    lv_obj_t *batt_label;
};

ui_status_bar_t *ui_status_bar_create(lv_obj_t *parent, const char *app_name)
{
    ui_status_bar_t *bar = calloc(1, sizeof(*bar));
    if (!bar) return NULL;

    lv_obj_t *bg = lv_obj_create(parent);
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, 320, 24);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_hex(UI_KIT_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    // 静态层:app 名,左对齐,画一次不再变
    lv_obj_t *name = lv_label_create(bg);
    lv_label_set_text(name, app_name ? app_name : "");
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(UI_KIT_COLOR_VALUE), 0);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 4, 0);

    // 动态层:uptime(居中)+ 电量/USB(右对齐)
    bar->uptime_label = lv_label_create(bg);
    lv_label_set_text(bar->uptime_label, "--:--");
    lv_obj_set_style_text_font(bar->uptime_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bar->uptime_label, lv_color_hex(UI_KIT_COLOR_LABEL), 0);
    lv_obj_align(bar->uptime_label, LV_ALIGN_CENTER, 0, 0);

    bar->batt_label = lv_label_create(bg);
    lv_label_set_text(bar->batt_label, "--%");
    lv_obj_set_style_text_font(bar->batt_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bar->batt_label, lv_color_hex(UI_KIT_COLOR_LABEL), 0);
    lv_obj_align(bar->batt_label, LV_ALIGN_RIGHT_MID, -4, 0);

    return bar;
}

void ui_status_bar_update(ui_status_bar_t *bar, uint32_t uptime_s, int batt_pct, bool vbus_present)
{
    if (!bar) return;

    uint32_t h = uptime_s / 3600, m = (uptime_s / 60) % 60, s = uptime_s % 60;
    if (h > 0) lv_label_set_text_fmt(bar->uptime_label, "%luh%02lum", (unsigned long)h, (unsigned long)m);
    else       lv_label_set_text_fmt(bar->uptime_label, "%02lu:%02lu", (unsigned long)m, (unsigned long)s);

    if (batt_pct < 0) lv_label_set_text_fmt(bar->batt_label, "--%% %s", vbus_present ? "USB" : "");
    else              lv_label_set_text_fmt(bar->batt_label, "%d%% %s", batt_pct, vbus_present ? "USB" : "");
}
