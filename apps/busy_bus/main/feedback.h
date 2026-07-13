// busy_bus —— 反馈编排器(SPEC.md §6):事件 → 音频/触觉/灯带/摇杆节点 RGB 四条
// 共享通道。屏幕(第五条"通道")由 bus_game/town/sprites 直接管,不经本模块
// (那些更新跟具体 LVGL 对象强绑定,详见 bus_game.c render_all 的说明)。
// game_task 只发事件(非阻塞),实际分发在后台任务里做,不阻塞游戏循环。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 起反馈后台任务 + 队列。需在 音频/震动/灯带/bus_link 各自初始化之后调。 */
esp_err_t feedback_init(void);

/** @brief 开机问候:暖音 + 轻震 + 灯带暖色。 */
void feedback_emit_hello(void);

/** @brief 撞障碍(房子/树/喷泉/屏边):轻 boing + 轻震(节流由调用方把关)。 */
void feedback_emit_bump(void);

/** @brief 接到乘客:上车叮咚 + 收集震 + 灯带扫光 + 摇杆节点变乘客目的地色。 */
void feedback_emit_pickup(uint32_t dest_color);

/** @brief 错门探头:中性"嗯?"上扬音,零负面反馈(不占触觉/灯带通道)。 */
void feedback_emit_wrong_door(void);

/** @brief 送达:到站铃 + 收集震 + 灯带定向扫 + 摇杆节点回暖白。 */
void feedback_emit_deliver(void);

/** @brief 喇叭:软包络双音 + 中震 + 摇杆节点闪白一下。 */
void feedback_emit_honk(bool passenger_nearby);

/** @brief 全送达派对:SND_WIN + 三连震 + 灯带彩虹(摇杆彩虹跑马由 bus_game 逐帧驱动)。 */
void feedback_emit_party(void);

#ifdef __cplusplus
}
#endif
