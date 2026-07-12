// 夜花园 v2 —— 静态层实现(见 garden.h)。P1 范围:进场画一次,之后不重画
// (CLAUDE.md §6 渲染红线)。P2 起会在这里加 5 个沉睡目标 + dwell/预亮/BLOOM/派对/
// 暮色编排,本文件先把静态层单独立好,不堵死加入点。
#include "garden.h"

#include <stddef.h>

#include "bsp/m5stack_core_2.h"

// ── 配色(§18 家族:大块扁平圆润 + 暖色;夜空用深蓝不用纯黑)───────────────────
#define SKY_COL       0x1B1B3D
#define GROUND_COL    0x232C46
#define MOON_COL      0xF4EDC9
#define STAR_COL      0xFFE89B
#define STEM_COL      0x2E5A3E
#define BUD_COL       0x6B4A73   // 沉睡花苞(暗紫,未苏醒——苏醒动画是 P2 范围)
#define BUD_SIDE_COL  0x54395E

// ── 小工具(纯 LVGL,调用方持锁)──────────────────────────────────────────
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

static void make_home_flower(lv_obj_t *parent)
{
    // 茎(接地面到花苞)
    lv_obj_t *stem = plain(parent, 6, 18, STEM_COL, 2);
    lv_obj_set_pos(stem, GARDEN_HOME_X - 3, GROUND_Y - 18);

    // 花苞(闭合未醒:两片侧瓣在下,主苞在上盖住——P1 只是萤火虫的"床",
    // 苏醒动画属于 P2 的 5 个目标之一,这里不实现)
    lv_obj_t *side_l = plain(parent, 12, 12, BUD_SIDE_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(side_l, GARDEN_HOME_X - 16, GARDEN_HOME_Y - 4);
    lv_obj_t *side_r = plain(parent, 12, 12, BUD_SIDE_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(side_r, GARDEN_HOME_X + 4, GARDEN_HOME_Y - 4);
    lv_obj_t *bud = plain(parent, 20, 20, BUD_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(bud, GARDEN_HOME_X - 10, GARDEN_HOME_Y - 10);
}

void garden_create(lv_obj_t *scr)
{
    bsp_display_lock(0);

    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(SKY_COL), 0);

    lv_obj_t *ground = plain(scr, SCREEN_W, GROUND_H, GROUND_COL, 0);
    lv_obj_set_pos(ground, 0, GROUND_Y);

    lv_obj_t *moon = plain(scr, 42, 42, MOON_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(moon, SCREEN_W - 60, 14);

    // 星星(静态,P1 不做逐帧闪烁——"画一次"字面落地,省渲染预算)
    static const int star_pos[][2] = {
        { 30, 30 }, { 80, 54 }, { 130, 20 }, { 200, 40 }, { 250, 70 }, { 60, 90 },
    };
    for (size_t i = 0; i < sizeof(star_pos) / sizeof(star_pos[0]); i++) {
        lv_obj_t *st = plain(scr, 6, 6, STAR_COL, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(st, star_pos[i][0], star_pos[i][1]);
    }

    make_home_flower(scr);

    bsp_display_unlock();
}
