// 旋钮忙碌台 —— 游戏主体(UI + 30Hz 轮询任务 + 反馈编排)
#pragma once

/** @brief 建 UI、探测 8Encoder、起 30Hz 游戏任务(core2_board_init 之后调)。 */
void knobs_game_start(void);
