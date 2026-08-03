// 迷宫数据 + 关卡库 + 碰撞 + 求解性校验 (CLAUDE.md §4, §19)
//
// 瓦片图(thick-wall):16 列 × 12 行,cell=20px,迷宫正好铺满 320×240(无偏移)。
// (2026-07-08 取消难度渐进:16 张同难度真迷宫,全部 1 格窄走廊 + 分叉/死胡同/环路;
//  tier/guide 字段随之删除。球 r=7、到家判定 GOAL_R=13 见 tuning.h。)
// 瓦片:'#'=墙  '.'=路面  'S'=起点  'H'=家  '*'=星  'X'=陷阱(踩中退回本关起点)
//
// 【2026-07-27 放弃零失败(仅本卡带,平台其它卡带仍守 CLAUDE.md §2 零失败)】
// 幼儿反馈已完全掌握原版(撞墙只是软弹、星非必需、16 关同难度洗牌),需要真实的
// "会输"来撑住挑战:静态陷阱格('X',踩中重来本关) + 巡逻怪(碰到重来本关)。
// trap 的 col=-1 表示本关无陷阱;失败代价只退回本关起点,不影响其它关卡进度
// (用户拍板,比生命值/连续惩罚温和)。
//
// 【2026-08-03 七写:巡逻怪加"绕圈"走法(hazard_t.loop),放进环路】
// 起因:用户实机看着 L7 的方形环回问"能不能让怪绕着这里一直走"。这不只是换个走法——
// 它解开了直线怪的死结:直线怪整段来回扫 1 格窄走廊,**没有绕路就等于永久墙**,所以
// 五写起被 verify_mazes.py 强制赶出所有必经通道,只能待在支路上,存在感天然薄。
// 环路上的怪则相反:环是图论上的**圈**,拿掉圈上任意一格,圈的另一条弧仍连通两侧,
// 所以绕圈怪**永远不可能把迷宫劈成两半**——它只是轮流给圈上每一格投下 ~1/8 圈时长的
// 阴影,还全程可见、周期恒定、能预判。于是绕圈怪可以**正大光明站在必经路上**,
// 玩法从"躲开杵在角落的怪"变成"看准它转到对面,趁机穿过去"。
// 16 关每关本来就有 1~2 个环(四写留的"给选路而非对错"),此前是无差别装饰;
// 放进怪之后,选哪条弧、什么时候进,第一次有了代价。
#pragma once

#include <stdbool.h>
#include "physics.h"   // vec2_t

#define MAZE_COLS   16
#define MAZE_ROWS   12
#define MAZE_CELL   20.0f

typedef struct { int col, row; } cell_t;

#define MAZE_HAZARD_PTS  6           // 一只怪的路径最多几个拐点(直线 2 / 方形环 4 / L6 的 6 拐角环 6)
#define MAZE_HAZARDS     2           // 一关最多几只怪

// 巡逻怪的路径:pts 相邻两点必须同行或同列(只走直角折线)。
//   loop=false → 沿 pts[0]…pts[n-1] 往返(triangle wave),两端各停 HAZARD_DWELL_MS;
//   loop=true  → pts[n-1] 接回 pts[0] 单向绕圈,不停(节奏靠周期恒定而非停顿来预判)。
// pts 的书写顺序即绕行方向:现为顺时针 10 只 / 逆时针 7 只,别让整袋 16 关都朝一个方向转。
typedef struct {
    cell_t pts[MAZE_HAZARD_PTS];
    int    n_pts;                    // 0 = 本槽位没怪
    bool   loop;
} hazard_t;

typedef struct {
    int          id;                 // 1~16
    const char  *grid[MAZE_ROWS];    // 每行 16 字符
    cell_t       start;
    cell_t       home;
    cell_t       stars[2];
    int          n_stars;
    cell_t       trap;               // 静态陷阱格;col=-1 表示本关无
    hazard_t     hazards[MAZE_HAZARDS];
    int          n_hazards;          // 0 = 本关无怪
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
