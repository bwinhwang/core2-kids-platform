// slingshot_feed —— 动态精灵:皮筋(兜 + 两股示意点)+ 果子(色相着色)+ 动物造型表
// (张嘴/闭嘴,仿 chain_lab PRIZE_LOOK 风格:身体圆 + 特征件 + 双眼 + 嘴)。
// SPEC.md §5/§7/§8:init 时把每只可能出现的精灵对象都预先建好(不运行时创建/销毁
// LVGL 对象),之后只换色/开合;不做运行时旋转贴图、不做运行时 alpha 混合。
//
// 动画所有权纪律(仿 busy_bus bus_game.c):挪坐标/缩放/显隐统一在 sling_game.c 的
// render_all() 一处做(直接操作下面结构体里暴露的 lv_obj_t 句柄,不经本文件包一层);
// 本文件只提供"建一次"(create)与"换装"(dress/mouth/cheeks/color,低频调用、自带锁)。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BAND_DOTS_MAX 4   // 上限(实际用几个由 tuning.h BAND_DOTS 决定,留余量声明数组)

// ── 皮筋(弹弓兜,拉动时跟手;发射后回落到锚点待命;门控期也跟手但不装果子)───────
typedef struct {
    lv_obj_t *pouch;
    lv_obj_t *dot_l[BAND_DOTS_MAX];
    lv_obj_t *dot_r[BAND_DOTS_MAX];
} band_sprite_t;

/** @brief 在 parent 下建皮筋(兜 + 两股各 BAND_DOTS 个小圆点)。调用方持有 bsp_display_lock。 */
void sprites_band_create(lv_obj_t *parent, band_sprite_t *out);

// ── 果子(色相着色,SPEC §1)──────────────────────────────────────────────
typedef struct {
    lv_obj_t *body;
    lv_obj_t *leaf;
} fruit_sprite_t;

/** @brief 在 parent 下建一颗果子(初始隐藏)。调用方持有 bsp_display_lock。 */
void sprites_fruit_create(lv_obj_t *parent, fruit_sprite_t *out);

/** @brief 染色(每次换新果子调一次;叶子固定绿色不随色相变)。自带锁(低频调用)。 */
void sprites_fruit_set_color(const fruit_sprite_t *f, uint8_t r, uint8_t g, uint8_t b);

// ── 动物(熊/小鸡/青蛙/兔/猫,张嘴/闭嘴;改进 C:每种独立五官+特征件+配色)──────────
typedef struct {
    lv_obj_t *container;
    lv_obj_t *body, *parta, *partb, *eye_l, *eye_r, *mouth, *cheek_l, *cheek_r;
    int       species;                    // 当前装扮的造型(dress 时写入)
    // 当前物种的五官基准(dress 写入):render_all 给眼睛加 ±EYE_TRACK_PX 偏移做"跟随瞄准",
    // mouth 开合复用嘴基准。都是 container 内局部坐标。
    int       eye_lx, eye_ly, eye_rx, eye_ry;
    int       mouth_x, mouth_y, mouth_w;
} animal_sprite_t;

/** @brief 在 parent 下建一只动物精灵(初始隐藏未装扮;container 已设好 transform pivot,
 *  供 GROW 态运行时 scale 用)。调用方持有 bsp_display_lock。 */
void sprites_animal_create(lv_obj_t *parent, animal_sprite_t *out);

/** @brief 按 species(0..ANIMAL_SPECIES-1)装扮身体/特征件/嘴色 + 显示。自带锁(低频调用)。 */
void sprites_animal_dress(animal_sprite_t *a, int species);

/** @brief 嘴开合(open=张嘴待喂 / closed=嚼一下)。自带锁(仅相位变化时调用,低频)。 */
void sprites_animal_mouth_open(animal_sprite_t *a, bool open);

/** @brief 腮红(满足表情,仅 EAT 态短暂显示)。自带锁(低频调用)。 */
void sprites_animal_cheeks(const animal_sprite_t *a, bool show);

/** @brief 取某物种的身体主色(改进 B:喂饱后好朋友排用同色的小精灵代表它)。 */
uint32_t sprites_species_body_color(int species);

#ifdef __cplusplus
}
#endif
