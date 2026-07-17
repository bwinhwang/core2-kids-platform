// 抓娃娃机 —— 游戏层(SPEC.md §3~§10):状态机 + 吊臂/爪子精灵 + 战利品/展示架 + 派对。
// 输入来自 chain_lab.h 的传输/绑定层(scan_bus/poll_enc/poll_joy 已在 game_task 里跑),
// 本层只消费 chain_lab_* getter、不碰 chain_bus/unit_chain_* 协议层(SPEC §1 红线)。
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 建抓娃娃机 UI(战利品坑/展示架/吊臂/爪子/无字提示卡),初始 PLAY_IDLE。 */
void crane_game_create(void);

/** @brief 每帧推进状态机 + 渲染(仅 AWAKE 态调用;chain_lab_* 读数需已刷新)。 */
void crane_game_tick(void);

/** @brief 节点在位状态变化时调(接管/失联/重扫后):刷新提示卡 + 重同步编码器基准,
 *         避免节点复位后计数跳变被误读成一次大幅转动。 */
void crane_game_sync_attach(void);

/** @brief 深度省电唤醒后:爪子/吊臂复位安全位置(收起 + 回中),取消进行中的抓取动画。 */
void crane_game_reset_position(void);

/** @brief 此刻是否允许摇杆自适应回中(见 chain_lab.c poll_joy):仅 PLAY_IDLE + 爪子在顶。
 *         下降/抓取/上升/落架/派对期间返回 false —— 冻结中心,免得孩子推着杆瞄准时
 *         偏移被吃成新中心、吊臂自己滑回屏幕中间。 */
bool crane_game_recenter_ok(void);

#ifdef __cplusplus
}
#endif
