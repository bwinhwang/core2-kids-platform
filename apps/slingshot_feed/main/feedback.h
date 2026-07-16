// slingshot_feed —— 反馈编排器(SPEC.md §6):事件 → 音频/触觉/灯带/摇杆节点 RGB 四条
// 共享通道。屏幕(第五条"通道")由 sling_game/meadow/sprites 直接管,不经本模块。
// game_task 只发事件(非阻塞),实际分发在后台任务里做,不阻塞游戏循环。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 起反馈后台任务 + 队列。需在 音频/震动/灯带/sling_link 各自初始化之后调。 */
esp_err_t feedback_init(void);

/** @brief 开机问候:暖音 + 轻震 + 灯带暖色。 */
void feedback_emit_hello(void);

/** @brief 发射:twang 弹射音 + 中震 + 节点闪一下。 */
void feedback_emit_fire(void);

/** @brief 命中吃:物种嗓音引子(改进 C)+ 五声音阶第 n 音 + 收集震 + 灯带扫光 + 节点绿闪。 */
void feedback_emit_eat(int n, int species);

/** @brief miss 落地:中性"噗"(节流)+ 轻震,零负面反馈。 */
void feedback_emit_miss(void);

/** @brief 动物长大:物种嗓音起头(改进 C)+ 上行欢呼 + 收集震 + 灯带定向扫。 */
void feedback_emit_grow(int species);

/** @brief 喂够批派对:SND_WIN + 三连震 + 灯带彩虹(摇杆彩虹跑马由 sling_game 逐帧驱动)。 */
void feedback_emit_party(void);

/** @brief 改进 A:瞄准弧对准嘴("瞄准锁定")时一声轻快"来嘛~",强化自教;上层已节流。 */
void feedback_emit_lock(void);

/** @brief 改进 A:动物久等无人喂时"还要~"催促(温柔下行 + 轻震)。 */
void feedback_emit_call(void);

#ifdef __cplusplus
}
#endif
