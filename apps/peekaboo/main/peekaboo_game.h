// 躲猫猫昼夜屋 —— 游戏主体(UI + 30Hz 任务 + DLight 采样 + 反馈编排)
#pragma once

/** @brief 建 UI、探测 DLight 单元、起 30Hz 游戏任务(core2_board_init 之后调)。 */
void peekaboo_game_start(void);
