// 喂怪兽 —— 游戏主体(UI + 30Hz 任务 + 超声波采样 + 反馈编排)
#pragma once

/** @brief 建 UI、探测超声波单元、起 30Hz 游戏任务(core2_board_init 之后调)。 */
void monster_game_start(void);
