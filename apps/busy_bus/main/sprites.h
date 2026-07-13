// busy_bus —— 巴士 8 朝向预烘精灵 + 乘客造型(仿 chain_lab PRIZE_LOOK)+ 房形图泡。
// SPEC.md §5「朝向精灵」§7「渲染预算合规」:init 时超采样烘一次,运行时只换图/挪坐标。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PASSENGER_LOOKS  4   // 熊 / 小鸡 / 青蛙 / 兔

/** @brief 烘焙巴士 8 朝向 ARGB8888 精灵(纯 CPU,一次性,无需持锁;app 启动时调一次)。 */
void sprites_bake_bus(void);

/** @brief 取第 heading(0..HEADING_COUNT-1)张巴士精灵。sprites_bake_bus() 之后有效。 */
const lv_image_dsc_t *sprites_bus(int heading);

/** @brief 巴士精灵边长(px,正方形)。 */
int sprites_bus_size(void);

// ── 乘客(身体圆 + 特征件 A/B + 双眼 + 嘴,全部不透明色块,LVGL 子对象自动裁剪)──────
typedef struct {
    lv_obj_t *container;   // 透明容器,整只一起移动/隐藏
    lv_obj_t *body, *parta, *partb, *eye_l, *eye_r, *mouth;
} passenger_sprite_t;

#define PASSENGER_W  20
#define PASSENGER_H  26

/** @brief 在 parent 下建一只乘客精灵(初始隐藏,未装扮)。 */
void sprites_passenger_create(lv_obj_t *parent, passenger_sprite_t *out);

/** @brief 按 look(0..PASSENGER_LOOKS-1) 装扮身体/特征件/嘴色。 */
void sprites_passenger_dress(const passenger_sprite_t *p, int look);

// ── 图泡(房形+颜色双编码,SPEC §2)──────────────────────────────────────
typedef struct {
    lv_obj_t *container;
    lv_obj_t *roof, *body;
} bubble_sprite_t;

#define BUBBLE_W  14
#define BUBBLE_H  16

/** @brief 在 parent 下建一个图泡(初始隐藏)。 */
void sprites_bubble_create(lv_obj_t *parent, bubble_sprite_t *out);

/** @brief 染色为目的房子颜色(屋身+屋顶同色,扁平优先)。 */
void sprites_bubble_set_color(const bubble_sprite_t *b, uint32_t color);

#ifdef __cplusplus
}
#endif
