// LVGL 渲染 (CLAUDE.md §9)。
// 三层模型:静态迷宫层(进关画一次)+ 动态球(每帧只刷脏矩形)。
// 红线:永不每帧整屏重绘。
#pragma once

#include "maze.h"

/** @brief 一次性建静态层容器 + 球对象。须在 bsp_display_start 之后。 */
void render_init(void);

/** @brief 画某关的静态迷宫层(背景/路面/家/星),并把球放回起点。进关调一次。 */
void render_load_level(const level_t *lvl);

/** @brief 非游戏画面(标题/校准):清掉迷宫,纯色背景,球留在屏上(位置由调用方设)。 */
void render_show_splash(uint32_t bg_hex);

/** @brief 把球移到 (cx,cy) 并复位形变/瞳孔(标题/校准用)。 */
void render_ball_set_pos(float cx, float cy);

/** @brief 游戏中每帧:移动 + 瞳孔朝速度方向 + 速度拉伸 + 撞墙挤扁(§5.1)。 */
void render_ball_update(float cx, float cy, float vx, float vy);

/** @brief 收集第 idx 颗星:放大淡出后删除。 */
void render_collect_star(int idx);

/** @brief 到家却没集齐星:把还没收的星闪几下指路(过关必收的提示,§14.2026-07-27)。 */
void render_hint_stars(void);

/** @brief 家的"兴奋态"总开关(球接近时,§5.2):脉动加快 **+ 换成亮灯态精灵**(屋里
 *         灯更亮、门口光斑更浓、光晕更厚)。两张精灵几何完全相同,换图不会位移。
 *         调用方仍只需给一个 fast(= 已经很近了),别再各管各的。 */
void render_home_excited(bool fast);

/** @brief 进/出休眠:删掉(恢复)家的无限脉动动画。
 *  屏都黑了 LVGL 还在按动画帧重绘 + flush,CPU 永远进不了 idle。 */
void render_set_sleeping(bool sleeping);

/** @brief 第 idx 只巡逻怪每帧移动(该只不存在则空操作);(cx,cy)为屏幕坐标。 */
void render_hazard_update(int idx, float cx, float cy);

// ── 特效层(事件触发,短生命周期,§9.1/§9.5)──────────────────────────
/** @brief 球被"顶"了一下:挤扁回弹(撞墙);下一帧 update 起效。 */
void render_ball_squash(void);
/** @brief 撞点泛光一下(单次淡出,非频闪);(cx,cy)为屏幕坐标。 */
void render_wall_flash(float cx, float cy);
/** @brief 踩陷阱/撞巡逻怪的死亡演出(2026-08-18):球炸没 + 白闪核 + 放射碎片 +
 *         危险红冲击环 + 世界震一下 + 四边红框告警,总时长 ~780ms。
 *         调用方须自己定格住游戏(game_state 的 ST_FAIL / FAIL_HOLD_MS),
 *         否则关卡会在演出中途被重置。(cx,cy)为死点屏幕坐标。 */
void render_fail_burst(float cx, float cy);
/** @brief 过关庆祝:少量彩纸轻柔飘落(限量,§9.5)。 */
void render_win_celebrate(void);
