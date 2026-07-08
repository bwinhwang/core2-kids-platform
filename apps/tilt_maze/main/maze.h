// 迷宫数据 + 关卡库 + 碰撞 + 求解性校验 (CLAUDE.md §4, §19)
//
// 瓦片图(thick-wall):16 列 × 12 行,cell=20px,迷宫正好铺满 320×240(无偏移)。
// (2026-07-08 取消难度渐进:16 张同难度真迷宫,全部 1 格窄走廊 + 分叉/死胡同/环路;
//  tier/guide 字段随之删除。球 r=7、到家判定 GOAL_R=13 见 tuning.h。)
// 瓦片:'#'=墙  '.'=路面  'S'=起点  'H'=家  '*'=星
#pragma once

#include <stdbool.h>
#include "physics.h"   // vec2_t

#define MAZE_COLS   16
#define MAZE_ROWS   12
#define MAZE_CELL   20.0f

typedef enum {
    WORLD_MEADOW = 0,   // 草地
    WORLD_SEASIDE,      // 海边
    WORLD_STARRY,       // 星空
    WORLD_CANDY,        // 糖果
} world_t;

typedef struct { int col, row; } cell_t;

typedef struct {
    int          id;                 // 1~16
    world_t      world;
    const char  *grid[MAZE_ROWS];    // 每行 16 字符
    cell_t       start;
    cell_t       home;
    cell_t       stars[2];
    int          n_stars;
} level_t;

/** @brief 关卡总数。 */
int maze_level_count(void);

/** @brief 取第 idx 关(0-based,循环时由调用方取模)。 */
const level_t *maze_get_level(int idx);

/** @brief 某格是否为墙(界外当墙处理)。 */
bool maze_is_wall(const level_t *lvl, int col, int row);

/** @brief 格中心的像素坐标。 */
vec2_t maze_cell_center(cell_t c);

/** @brief BFS 起点→家 可解性校验(§4.1)。 */
bool maze_is_solvable(const level_t *lvl);

// 碰撞结果(供反馈编排器决定震动/音效力度)
typedef struct {
    bool  hit;          // 本次是否撞墙
    float speed;        // 撞击强度 = 法向速度(px/s)
} maze_collision_t;

/**
 * @brief 圆 vs 墙格 的滑行式碰撞解算(§4.4)。
 *        抵消法向速度分量(带微回弹)、保留切向分量(沿墙滑行),把球推出墙体。
 *        直接就地修改 pos/vel。
 */
maze_collision_t maze_resolve_collision(const level_t *lvl, vec2_t *pos, vec2_t *vel, float r);

/** @brief 球心是否到家(dist < GOAL_R)。 */
bool maze_reached_home(const level_t *lvl, vec2_t pos);
