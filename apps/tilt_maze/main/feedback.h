// 反馈编排器 (CLAUDE.md §6)。把"事件"分发到四条通道:视觉/音频/触觉/灯带。
// game_task 只发事件(非阻塞),实际分发在后台任务里做,不阻塞游戏循环(§3.2)。
#pragma once

#include "esp_err.h"

/** @brief 起反馈后台任务 + 队列。需在 音频/震动/灯带 各组件 init 之后调。 */
esp_err_t feedback_init(void);

/** @brief 开机问候:暖音 + 轻震 + 灯带暖色。 */
void feedback_emit_hello(void);

/** @brief 撞墙:强度=法向速度(px/s),分轻/中/重;(x,y)为屏幕撞点。 */
void feedback_emit_bump(float speed, float x, float y);

/** @brief 接近目标:level 1~3 越大越近;level 0 表示离开接近区。 */
void feedback_emit_near(int level);

/** @brief 收集到星:(x,y)为星位置。 */
void feedback_emit_collect(float x, float y);

/** @brief 到达目标(过关):全通道庆祝。 */
void feedback_emit_win(void);
