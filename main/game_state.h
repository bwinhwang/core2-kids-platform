// 游戏状态机 (CLAUDE.md §7, §20.9)。
// ATTRACT → PLAY → WIN →(下一关)→ PLAY,关卡循环(绝对零点免校准,无 CALIBRATE 态)。
// 打盹/深度省电/唤醒由 core2_sleep 组件在 game_task 里托管。
#pragma once

#include <stdbool.h>

/**
 * @brief 起状态机任务(从 ATTRACT 开始)。
 *        须在 IMU / 渲染 / 反馈各组件 init 之后调。
 */
void game_state_start(void);

// ── 供家长菜单(parent_menu)调用 ────────────────────────────────────
/** @brief 暂停/恢复游戏(菜单打开时暂停物理)。 */
void game_state_set_paused(bool paused);

/** @brief 设难度档:可玩关数(2=仅 L1~L2 简单档;4=全 4 关)。 */
void game_state_set_level_band(int max_levels);

/** @brief 设常态背光(%),并立即应用;打盹唤醒后恢复到此值。 */
void game_state_set_play_brightness(int pct);
