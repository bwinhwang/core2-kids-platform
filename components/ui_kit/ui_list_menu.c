#include "ui_kit.h"

#include <stdlib.h>

#define ROW_H 32

typedef struct {
    lv_obj_t *row;
    lv_obj_t *label;
    lv_obj_t *sw;   // NULL 若该行没挂开关
} row_t;

struct ui_list_menu_s {
    lv_obj_t *container;
    row_t     rows[UI_LIST_MENU_MAX_ROWS];
    int       row_count;
    ui_list_menu_cb_t cb;
    void     *cb_user_data;
};

// 事件回调只拿得到 lv_event_get_user_data(），把 (menu, row_idx) 编码进一个静态数组的下标里:
// user_data 直接传该行在 menu->rows 里的 index(intptr_t),menu 指针另存在 lv_obj 的
// user_data 里取不到时改用闭包表——这里选更简单的办法:menu 与 row_idx 一起打包进一个
// 小上下文结构体,随行创建时分配、随 menu 生命周期存活(menu 不销毁,数组常驻,无需释放)。
typedef struct {
    ui_list_menu_t *menu;
    int              row_idx;
} click_ctx_t;

static click_ctx_t s_ctx_pool[UI_LIST_MENU_MAX_ROWS * 8];  // 8 个 menu 实例的上限(评估台单屏够用)
static int s_ctx_pool_used = 0;

static click_ctx_t *alloc_ctx(ui_list_menu_t *menu, int row_idx)
{
    if (s_ctx_pool_used >= (int)(sizeof(s_ctx_pool) / sizeof(s_ctx_pool[0]))) return NULL;
    click_ctx_t *c = &s_ctx_pool[s_ctx_pool_used++];
    c->menu = menu;
    c->row_idx = row_idx;
    return c;
}

static void row_click_cb(lv_event_t *e)
{
    click_ctx_t *ctx = (click_ctx_t *)lv_event_get_user_data(e);
    if (ctx && ctx->menu && ctx->menu->cb) ctx->menu->cb(ctx->row_idx, ctx->menu->cb_user_data);
}

ui_list_menu_t *ui_list_menu_create(lv_obj_t *parent, int x, int y, int w, int h)
{
    ui_list_menu_t *menu = calloc(1, sizeof(*menu));
    if (!menu) return NULL;

    menu->container = lv_obj_create(parent);
    lv_obj_remove_style_all(menu->container);
    lv_obj_set_size(menu->container, w, h);
    lv_obj_set_pos(menu->container, x, y);
    lv_obj_set_style_bg_color(menu->container, lv_color_hex(UI_KIT_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(menu->container, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(menu->container, 8, 0);
    lv_obj_add_flag(menu->container, LV_OBJ_FLAG_SCROLLABLE);

    return menu;
}

int ui_list_menu_add_row(ui_list_menu_t *menu, const char *text, bool with_switch)
{
    if (!menu || menu->row_count >= UI_LIST_MENU_MAX_ROWS) return -1;
    int idx = menu->row_count++;
    row_t *r = &menu->rows[idx];

    r->row = lv_obj_create(menu->container);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_size(r->row, LV_PCT(100), ROW_H);
    lv_obj_set_pos(r->row, 0, idx * ROW_H);
    lv_obj_set_style_bg_opa(r->row, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r->row, LV_OBJ_FLAG_CLICKABLE);

    r->label = lv_label_create(r->row);
    lv_label_set_text(r->label, text ? text : "");
    lv_obj_set_style_text_font(r->label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(r->label, lv_color_hex(UI_KIT_COLOR_VALUE), 0);
    lv_obj_align(r->label, LV_ALIGN_LEFT_MID, 6, 0);

    click_ctx_t *ctx = alloc_ctx(menu, idx);
    lv_obj_add_event_cb(r->row, row_click_cb, LV_EVENT_CLICKED, ctx);

    if (with_switch) {
        r->sw = lv_switch_create(r->row);
        lv_obj_align(r->sw, LV_ALIGN_RIGHT_MID, -6, 0);
        lv_obj_add_event_cb(r->sw, row_click_cb, LV_EVENT_VALUE_CHANGED, ctx);
    }

    return idx;
}

void ui_list_menu_set_row_text(ui_list_menu_t *menu, int row_idx, const char *text)
{
    if (!menu || row_idx < 0 || row_idx >= menu->row_count) return;
    lv_label_set_text(menu->rows[row_idx].label, text ? text : "");
}

bool ui_list_menu_get_switch(ui_list_menu_t *menu, int row_idx)
{
    if (!menu || row_idx < 0 || row_idx >= menu->row_count) return false;
    lv_obj_t *sw = menu->rows[row_idx].sw;
    if (!sw) return false;
    return lv_obj_has_state(sw, LV_STATE_CHECKED);
}

void ui_list_menu_on_click(ui_list_menu_t *menu, ui_list_menu_cb_t cb, void *user_data)
{
    if (!menu) return;
    menu->cb = cb;
    menu->cb_user_data = user_data;
}
