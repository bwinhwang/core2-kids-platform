// sprites —— fish_pond 全部精灵的"预烘"构件(SPEC.md §6.6)
//
// 本仓库的"预烘"手法(沿用 chain_lab/crane_game.c PRIZE_LOOK 先例):init 时用扁平色块
// 组合搭好一只精灵的全部子对象(身体/眼/嘴/尾…),之后只在状态切换时离散地改子对象的
// 尺寸/位置/颜色(不做运行时 alpha 混合、不做逐帧插值形变)——满足"状态帧切换 = 预烘
// 交换"的精神,同时完全用现有 LVGL 原语实现(不依赖 lv_canvas/图片资源)。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// 鱼种(同时是展示架/木桶等模块共用的索引)
typedef enum {
    FISH_SPECIES_FAT = 0,    // 胖胖鱼(浅层)
    FISH_SPECIES_LAZY,       // 大懒鱼(深层)
    FISH_SPECIES_COUNT,
} fish_species_t;

// 朝向(面向摆动方向;子对象左右镜像用)
typedef enum {
    FISH_FACE_LEFT  = -1,
    FISH_FACE_RIGHT = 1,
} fish_face_t;

// 鱼的离散视觉状态(SPEC §6.2 表情戏剧;张嘴两级 = 两个预烘尺寸)
typedef enum {
    FISH_VIS_PATROL_A = 0,
    FISH_VIS_PATROL_B,     // 游动尾摆第二帧(氛围档,~2fps 切换)
    FISH_VIS_NOTICE,       // 瞪眼
    FISH_VIS_APPROACH_SMALL,
    FISH_VIS_APPROACH_BIG,
    FISH_VIS_CHOMP,        // 咬合(HOOKED 沿用此态)
    FISH_VIS_GIGGLE,       // 咯咯(嘴闭、眼放松)
} fish_vis_t;

// 一只鱼精灵:透明容器 + 身体/尾鳍/双眼/嘴子对象,整只随 set_pos 一起移动
typedef struct {
    lv_obj_t *root;
    lv_obj_t *tail;
    lv_obj_t *body;
    lv_obj_t *belly;    // 静态装饰(下腹浅色斑,create 时摆一次,不随状态变)
    lv_obj_t *eye_l, *eye_r;
    lv_obj_t *mouth;
    int       w, h;
} fpond_fish_sprite_t;

/** @brief 通用扁平色块矩形(圆角可传 LV_RADIUS_CIRCLE 做胶囊/圆形)。 */
lv_obj_t *fpond_box(lv_obj_t *parent, int w, int h, uint32_t color, int radius);

/** @brief 建一只鱼精灵(size 由调用方按鱼种传 FISH_FAT_ 或 FISH_LAZY_ 系列常量)。初始隐藏。 */
void fpond_fish_sprite_create(fpond_fish_sprite_t *s, lv_obj_t *parent,
                               int w, int h, uint32_t body_col, uint32_t belly_col);

/** @brief 按离散状态 + 朝向重绘子对象(尺寸/位置/颜色,零运行时 alpha)。 */
void fpond_fish_sprite_set_state(fpond_fish_sprite_t *s, fish_vis_t vis, fish_face_t face);

/** @brief 定位整只鱼(中心坐标)。 */
void fpond_fish_sprite_set_pos(fpond_fish_sprite_t *s, int cx, int cy);

void fpond_fish_sprite_show(fpond_fish_sprite_t *s, bool show);

// ── 船 ───────────────────────────────────────────────────────────────
typedef struct {
    lv_obj_t *hull;
    lv_obj_t *cabin;
} fpond_boat_sprite_t;

void fpond_boat_sprite_create(fpond_boat_sprite_t *s, lv_obj_t *parent);
/** @brief cx = 船中心 x;top_y = 船顶 y;paddle = true 时摇臂帧(小小晃一下,纯氛围)。 */
void fpond_boat_sprite_set_pos(fpond_boat_sprite_t *s, int cx, int top_y, bool paddle);

// ── 饵 ───────────────────────────────────────────────────────────────
typedef struct {
    lv_obj_t *body;
    lv_obj_t *highlight;
} fpond_bait_sprite_t;

void fpond_bait_sprite_create(fpond_bait_sprite_t *s, lv_obj_t *parent);
void fpond_bait_sprite_set_pos(fpond_bait_sprite_t *s, int cx, int cy);

// ── 小装饰(泡泡/水花圈,纯装饰豁免 §8)─────────────────────────────────────
lv_obj_t *fpond_dot_create(lv_obj_t *parent, int d, uint32_t color);

#ifdef __cplusplus
}
#endif
