// slingshot_feed —— 游戏层(SPEC.md §4~§10):状态机 + 拉-放检测(§5.1)+ 弹道积分/
// 预览(§5.2)+ 命中/喂饱/长大(§5.3/§5.4)+ miss/派对(§5.5/§6)。输入来自
// sling_link.h(scan/poll 已在 game_task 里跑),本层只消费 sling_link_* getter,
// 不碰 chain_bus/unit_chain_* 协议层。
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 建草地 UI(meadow_create)+ 弹弓/果子/动物精灵 + 提示卡,初始 AIM 态。 */
void sling_game_create(void);

/** @brief 每帧推进状态机 + 渲染(仅 AWAKE 态调用;sling_link_* 读数需已刷新)。 */
void sling_game_tick(void);

/** @brief 节点在位状态变化时调(接管/失联/重扫后):刷新提示卡。 */
void sling_game_sync_attach(void);

/** @brief 深度省电唤醒后:弹弓复位待命、飞行中果子作废重填、当前动物保留、已喂饱
 *  进度不清(SPEC §10"复位安全位置、不吞进度"原则)。 */
void sling_game_reset_after_wake(void);

#ifdef __cplusplus
}
#endif
