// 迷宫数据 + 关卡库 + 碰撞 + 求解性校验 (CLAUDE.md §4, §19)
//
// 瓦片图(thick-wall):16 列 × 12 行,cell=20px,迷宫正好铺满 320×240(无偏移)。
// (2026-07-08 取消难度渐进:16 张同难度真迷宫,全部 1 格窄走廊 + 分叉/死胡同/环路;
//  tier/guide 字段随之删除。球 r=7、到家判定 GOAL_R=13 见 tuning.h。)
// 瓦片:'#'=墙  '.'=路面  'S'=起点  'H'=家  '*'=星  'X'=陷阱(踩中退回本关起点)
//
// 【2026-07-27 放弃零失败(仅本卡带,平台其它卡带仍守 CLAUDE.md §2 零失败)】
// 幼儿反馈已完全掌握原版(撞墙只是软弹、星非必需、16 关同难度洗牌),需要真实的
// "会输"来撑住挑战:静态陷阱格('X',踩中重来本关) + 可选一只巡逻怪(hazard_a↔hazard_b
// 直线往返,碰到重来本关)。trap/hazard_a 的 col=-1 表示本关未使用该机制。
// 失败代价只退回本关起点,不影响其它关卡进度(用户拍板,比生命值/连续惩罚温和)。
#pragma once

#include <stdbool.h>
#include "physics.h"   // vec2_t

#define MAZE_COLS   16
#define MAZE_ROWS   12
#define MAZE_CELL   20.0f

typedef struct { int col, row; } cell_t;

typedef struct {
    int          id;                 // 1~16
    const char  *grid[MAZE_ROWS];    // 每行 16 字符
    cell_t       start;
    cell_t       home;
    cell_t       stars[2];
    int          n_stars;
    cell_t       trap;               // 静态陷阱格;col=-1 表示本关无
    cell_t       hazard_a;           // 巡逻怪往返端点 A(与 B 同行/同列直线);col=-1 表示本关无
    cell_t       hazard_b;           // 端点 B
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
