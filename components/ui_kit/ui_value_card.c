#include "ui_kit.h"

#include <stdlib.h>
#include <stdio.h>

struct ui_value_card_s {
    lv_obj_t *value_label;
};

ui_value_card_t *ui_value_card_create(lv_obj_t *parent, int x, int y, int w, int h,
                                       const char *label, const char *unit)
{
    ui_value_card_t *card = calloc(1, sizeof(*card));
    if (!card) return NULL;

    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_KIT_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_CLICKABLE);

    // 静态层:标签(顶部)+ 单位(底部),画一次不再变
    lv_obj_t *lbl = lv_label_create(panel);
    lv_label_set_text(lbl, label ? label : "");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_KIT_COLOR_LABEL), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 6, 4);

    if (unit && unit[0]) {
        lv_obj_t *u = lv_label_create(panel);
        lv_label_set_text(u, unit);
        lv_obj_set_style_text_font(u, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(u, lv_color_hex(UI_KIT_COLOR_LABEL), 0);
        lv_obj_align(u, LV_ALIGN_BOTTOM_RIGHT, -6, -4);
    }

    // 动态层:核心数字(每次 set_value/set_error 只改这个 label 的 text/color/font)
    card->value_label = lv_label_create(panel);
    lv_label_set_text(card->value_label, "--");
    lv_obj_set_style_text_font(card->value_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(card->value_label, lv_color_hex(UI_KIT_COLOR_VALUE), 0);
    lv_obj_align(card->value_label, LV_ALIGN_CENTER, 0, 4);

    return card;
}

void ui_value_card_set_value(ui_value_card_t *card, float value, const ui_value_card_thresh_t *thresh)
{
    if (!card) return;

    bool warn = thresh && thresh->enabled && (value < thresh->warn_lo || value > thresh->warn_hi);

    lv_obj_set_style_text_font(card->value_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(card->value_label,
                                lv_color_hex(warn ? UI_KIT_COLOR_WARN : UI_KIT_COLOR_VALUE), 0);

    // 数值卡显示到小数点后一位;整数值(如 mV/mA)天然多一位无意义小数,调用方若想要纯整数
    // 可自行先四舍五入再传入(这里为通用性统一保留一位小数)。
    lv_label_set_text_fmt(card->value_label, "%.1f", (double)value);
}

void ui_value_card_set_error(ui_value_card_t *card, const char *err_text)
{
    if (!card) return;
    // 错误显式呈现(CLAUDE.md §2 原则 2):告警色 + 换小一号字体(错误文字通常比数字长)
    lv_obj_set_style_text_font(card->value_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(card->value_label, lv_color_hex(UI_KIT_COLOR_WARN), 0);
    lv_label_set_text(card->value_label, err_text ? err_text : "ERR");
}
