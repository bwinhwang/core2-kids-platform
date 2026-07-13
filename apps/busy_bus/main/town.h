// busy_bus —— 静态层:小镇全景(马路装饰/三栋彩色房子两态/树+喷泉障碍/站牌表/车库)
// 进场画一次,之后只有房子"亮灯"两态切换(渲染红线 CLAUDE.md §6)。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { float x, y; } vec2_t;
typedef struct { float x, y, w, h; } rect_t;
typedef struct { float cx, cy, r; } circle_t;

#define TOWN_HOUSES     3
#define TOWN_STOPS      5
#define TOWN_TREES      2
#define TOWN_OBSTACLES  (TOWN_TREES + 1)   // 树×2 + 喷泉,圆形障碍

/** @brief 建静态层(挂在 parent,通常是 lv_screen_active())。只调一次。 */
void town_create(lv_obj_t *parent);

/** @brief 第 idx 栋房子亮灯(送达/派对)/暗窗(未送达)两态,预画切换不做运行时发光。 */
void town_house_set_lit(int idx, bool lit);

/** @brief 第 idx 栋房子窗口眨一下("这家不是哦",错门反馈用)。 */
void town_house_blink(int idx);

/** @brief 房子主色(0xRRGGBB),供图泡/乘客目的地双编码复用。 */
uint32_t town_house_color(int idx);

/** @brief 房子中心坐标(px,图泡/乘客落点参考)。 */
vec2_t town_house_center(int idx);

/** @brief 房子门垫矩形(送达判定区,DOOR_ZONE_W×H)。 */
rect_t town_house_door(int idx);

/** @brief 房子本体矩形(碰撞障碍,撞上滑行不粘住)。 */
rect_t town_house_body(int idx);

/** @brief 第 idx 个站牌点坐标(0..TOWN_STOPS-1;每轮从中随机选 PASSENGERS_PER_ROUND 个)。 */
vec2_t town_stop_point(int idx);

/** @brief 车库(巴士起点 / 深度省电唤醒复位点)坐标。 */
vec2_t town_garage_pos(void);

/** @brief 圆形障碍表(树×2 + 喷泉),供碰撞滑行用。 */
const circle_t *town_obstacles(int *count);

#ifdef __cplusplus
}
#endif
