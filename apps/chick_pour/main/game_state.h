// 游戏状态机(CLAUDE.md §7,仿 tilt_maze game_state.c 骨架)。
//
// P2 范围(SPEC §12):归家闭环 —— PLAY(门判定/捕获/弹出/计数)→ 全归家 PARTY
// (倒计时 PARTY_HOLD_MS)→ 重散一批回 PLAY,循环。ATTRACT(睡醒)属 P3,
// 开机即直接进 PLAY;打盹/深度省电 core2_sleep 全托管,唤醒回当前进度。
#pragma once

/** @brief 起状态机任务:建 10 只动物(物理+精灵)+ core2_sleep 托管 + 60Hz game_task。
 *         须在 scene_init() / feedback_init() 之后调。 */
void game_state_start(void);
