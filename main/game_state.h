// 游戏状态机 (CLAUDE.md §7)。
// ATTRACT → CALIBRATE → PLAY → WIN →(下一关)→ PLAY,关卡循环。
// IDLE:PLAY 中长时间无动作 → 打盹(降亮省电);动一下 → 唤醒回 PLAY。
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

/** @brief 请求重新校准:下次恢复时进 CALIBRATE。 */
void game_state_request_recalibrate(void);

/** @brief 设难度档:可玩关数(2=仅 L1~L2 简单档;4=全 4 关)。 */
void game_state_set_level_band(int max_levels);

/** @brief 设常态背光(%),并立即应用;打盹唤醒后恢复到此值。 */
void game_state_set_play_brightness(int pct);
