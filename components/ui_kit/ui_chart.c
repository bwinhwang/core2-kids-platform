#include "ui_kit.h"

#include <stdlib.h>

struct ui_chart_s {
    lv_obj_t            *chart;
    lv_chart_series_t   *series;
};

ui_chart_t *ui_chart_create(lv_obj_t *parent, int x, int y, int w, int h,
                             int point_count, int32_t y_min, int32_t y_max)
{
    ui_chart_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    c->chart = lv_chart_create(parent);
    lv_obj_set_size(c->chart, w, h);
    lv_obj_set_pos(c->chart, x, y);
    lv_obj_set_style_bg_color(c->chart, lv_color_hex(UI_KIT_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(c->chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c->chart, 0, 0);
    lv_obj_set_style_line_color(c->chart, lv_color_hex(0x3A3F45), LV_PART_MAIN);  // 网格线暗于面板
    lv_chart_set_type(c->chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(c->chart, 3, 3);
    lv_chart_set_point_count(c->chart, point_count > 0 ? point_count : 60);
    lv_chart_set_axis_range(c->chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
    lv_chart_set_update_mode(c->chart, LV_CHART_UPDATE_MODE_SHIFT);  // 环形推点(新点从右侧进,整体左移)

    c->series = lv_chart_add_series(c->chart, lv_color_hex(UI_KIT_COLOR_ACCENT), LV_CHART_AXIS_PRIMARY_Y);

    return c;
}

void ui_chart_push(ui_chart_t *chart, int32_t value)
{
    if (!chart) return;
    lv_chart_set_next_value(chart->chart, chart->series, value);
}
