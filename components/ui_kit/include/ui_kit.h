// ui_kit —— 评估台 UI 控件(状态栏 / 数值卡 / chart / 列表菜单)
//
// 全守 CLAUDE.md §6 渲染红线:每个控件创建时画一次静态层(边框/标签),之后的 update/push
// 只改动态部分(LVGL label 重设 text、chart 环形推点),天然只脏自身矩形,不整屏重绘。
// v1 全 ASCII(CJK 字体暂不引入,见 CLAUDE.md §8 / docs/ROADMAP.md 风险记录),用
// `CONFIG_LV_FONT_MONTSERRAT_16`(状态栏/标签)+ `_24`(数值卡核心数字),两个字体宏已进
// `sdkconfig.platform`。
//
// ⚠️ **本组件不内部加 `bsp_display_lock/unlock`**——与本仓其它组件一致(audio_fx/haptics
// 不碰 LVGL,launcher 的 `ui_create()` 自己在外层包一次锁做一批创建):调用方负责在自己的
// LVGL 操作外包锁,可以一次锁里批量调用多个 ui_kit 函数,不会因为内部重复加锁而死锁。
//
// 配色(深灰工程风,评估台通用,与幼儿掌机时期马卡龙色系区分):
//   面板背景 UI_KIT_COLOR_PANEL(0x2A2E33)、数值文字 UI_KIT_COLOR_VALUE(0xE8E8E8)、
//   标签/次要文字 UI_KIT_COLOR_LABEL(0x9AA0A6)、告警色 UI_KIT_COLOR_WARN(0xE05A4E)。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_KIT_COLOR_PANEL  0x2A2E33
#define UI_KIT_COLOR_VALUE  0xE8E8E8
#define UI_KIT_COLOR_LABEL  0x9AA0A6
#define UI_KIT_COLOR_WARN   0xE05A4E
#define UI_KIT_COLOR_ACCENT 0x4FB0D8

// ── 状态栏:顶 24px,app 名 + uptime + 电量,1Hz 更新只脏自己 ────────────────────
typedef struct ui_status_bar_s ui_status_bar_t;

/** @brief 在 parent(通常是活动屏)顶部创建 320×24 状态栏,静态层(背景/app 名)画一次。 */
ui_status_bar_t *ui_status_bar_create(lv_obj_t *parent, const char *app_name);

/** @brief 更新 uptime/电量/USB 状态(只改数字标签的 text,小脏矩形)。
 *  @param batt_pct 0~100,<0 表示"未知"(显示 "--"，如 power_monitor 还没就绪)。 */
void ui_status_bar_update(ui_status_bar_t *bar, uint32_t uptime_s, int batt_pct, bool vbus_present);

// ── 数值卡:标签 + 大数值 + 单位,阈值变色 ──────────────────────────────────────
typedef struct ui_value_card_s ui_value_card_t;

typedef struct {
    bool  enabled;   // false = 不做阈值判定,数值恒用正常色
    float warn_lo;   // < warn_lo 或 > warn_hi 时变告警色
    float warn_hi;
} ui_value_card_thresh_t;

/** @brief 创建一张数值卡(静态层——边框/标签/单位画一次)。 */
ui_value_card_t *ui_value_card_create(lv_obj_t *parent, int x, int y, int w, int h,
                                       const char *label, const char *unit);

/** @brief 更新数值(只重设数字文本,小脏矩形)。thresh 传 NULL = 不做阈值判定。 */
void ui_value_card_set_value(ui_value_card_t *card, float value, const ui_value_card_thresh_t *thresh);

/** @brief 错误显式呈现(CLAUDE.md §2 原则 2):数值区改显告警色文字,如 "TIMEOUT"。 */
void ui_value_card_set_error(ui_value_card_t *card, const char *err_text);

// ── chart:lv_chart 封装,环形推点(2~10Hz,CLAUDE.md §6.6) ─────────────────────
typedef struct ui_chart_s ui_chart_t;

/**
 * @brief 创建一个折线图(静态层——边框/坐标范围一次性设好)。
 * @param point_count 环形缓冲点数(按屏幕分配宽度定,~60~90 点足够看出趋势)。
 * @param y_min/y_max 纵轴范围(取整,LVGL chart 内部值域是 int32_t;需要小数精度的
 *        物理量由调用方预先按固定倍数放大,如 lux*10,再在自己的数值卡上还原)。
 */
ui_chart_t *ui_chart_create(lv_obj_t *parent, int x, int y, int w, int h,
                             int point_count, int32_t y_min, int32_t y_max);

/** @brief 环形推入一个新点(只重绘新增点影响的窄条,LVGL dirty-rect 天生如此)。 */
void ui_chart_push(ui_chart_t *chart, int32_t value);

// ── 列表菜单:可点击行,行内可挂开关 ────────────────────────────────────────────
#define UI_LIST_MENU_MAX_ROWS 12

typedef struct ui_list_menu_s ui_list_menu_t;
typedef void (*ui_list_menu_cb_t)(int row_idx, void *user_data);

/** @brief 创建一个可滚动列表容器(静态层——背景/边框一次性设好)。 */
ui_list_menu_t *ui_list_menu_create(lv_obj_t *parent, int x, int y, int w, int h);

/** @brief 追加一行(静态层:行创建一次)。with_switch=true 时行右侧挂一个 lv_switch。
 *  @return 行号(0 起);已达 UI_LIST_MENU_MAX_ROWS 时返回 -1。 */
int ui_list_menu_add_row(ui_list_menu_t *menu, const char *text, bool with_switch);

/** @brief 改某一行的文字(如扫描结果地址→单元名,动态层,只脏该行标签)。 */
void ui_list_menu_set_row_text(ui_list_menu_t *menu, int row_idx, const char *text);

/** @brief 读某一行开关状态(该行须是 with_switch=true 创建的,否则恒返回 false)。 */
bool ui_list_menu_get_switch(ui_list_menu_t *menu, int row_idx);

/** @brief 注册整个列表的点击回调(点哪行、点开关都会触发,回调里用 row_idx 分流)。 */
void ui_list_menu_on_click(ui_list_menu_t *menu, ui_list_menu_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif
