// busy_bus —— 游戏层(SPEC.md §4~§6):状态机 + 巴士速度积分/朝向 + 障碍滑行 +
// 接送判定 + 乘客子状态机 + 喇叭 + 派对。输入来自 bus_link.h(scan/poll 已在
// game_task 里跑),本层只消费 bus_link_* getter,不碰 chain_bus/unit_chain_* 协议层。
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 建小镇 UI(town_create)+ 巴士/乘客/图泡精灵 + 提示卡,初始 ROUND_SETUP。 */
void bus_game_create(void);

/** @brief 每帧推进状态机 + 渲染(仅 AWAKE 态调用;bus_link_* 读数需已刷新)。 */
void bus_game_tick(void);

/** @brief 节点在位状态变化时调(接管/失联/重扫后):刷新提示卡。 */
void bus_game_sync_attach(void);

/** @brief 深度省电唤醒后:巴士复位安全位置(车库),不吞送达进度(SPEC §10)。 */
void bus_game_reset_position(void);

#ifdef __cplusplus
}
#endif
