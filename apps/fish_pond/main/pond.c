// pond —— 静态层实现(SPEC.md §2 ASCII 布局 / §6.5 静态层进关烘一次)
#include "pond.h"

#include "bsp/m5stack_core_2.h"

#include "sprites.h"
#include "tuning.h"

#define SCREEN_W 320
#define SCREEN_H 240

void pond_create(lv_obj_t *scr)
{
    bsp_display_lock(0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_SKY), 0);

    // 天空 + 太阳(ASCII §2:左上角 ☀)
    fpond_box(scr, SCREEN_W, WATERLINE_Y, COL_SKY, 0);
    lv_obj_t *sun = fpond_box(scr, 32, 32, COL_SUN, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(sun, 20, 6);

    // 浅层水带 / 深层水带
    lv_obj_t *shallow = fpond_box(scr, SCREEN_W, LAYER_SPLIT_Y - WATERLINE_Y, COL_SHALLOW, 0);
    lv_obj_set_pos(shallow, 0, WATERLINE_Y);
    lv_obj_t *deep = fpond_box(scr, SCREEN_W, SCREEN_H - LAYER_SPLIT_Y, COL_DEEP, 0);
    lv_obj_set_pos(deep, 0, LAYER_SPLIT_Y);

    // 水线泡沫(细亮条)+ 层分界暗线(纯装饰,烘一次不再动)
    lv_obj_t *foam = fpond_box(scr, SCREEN_W, 3, COL_WATERLINE_FOAM, 0);
    lv_obj_set_pos(foam, 0, WATERLINE_Y - 1);
    lv_obj_t *split = fpond_box(scr, SCREEN_W, 2, COL_LAYER_SPLIT, 0);
    lv_obj_set_pos(split, 0, LAYER_SPLIT_Y - 1);

    // 水草 / 石头(纯装饰,~12px,SPEC §3 表明文豁免)
    static const int grass_x[] = { 30, 70, 150, 260 };
    for (unsigned i = 0; i < sizeof(grass_x) / sizeof(grass_x[0]); i++) {
        lv_obj_t *g = fpond_box(scr, 8, 20, COL_GRASS, 4);
        lv_obj_set_pos(g, grass_x[i], SCREEN_H - 22);
    }
    static const int stone_x[] = { 100, 200, 300 };
    for (unsigned i = 0; i < sizeof(stone_x) / sizeof(stone_x[0]); i++) {
        lv_obj_t *st = fpond_box(scr, 14, 10, COL_STONE, 5);
        lv_obj_set_pos(st, stone_x[i], SCREEN_H - 14);
    }
    bsp_display_unlock();
}
