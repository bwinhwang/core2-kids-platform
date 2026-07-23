// feedback —— 事件 → 四通道编排(SPEC.md §7 反馈矩阵)
//
// audio_fx / haptics / ledstrip_fx 三个组件本身已是"投队列非阻塞",本层不需要再起
// 独立任务/队列包一层——直接调用即是"绝不阻塞 game_task"。本文件只做语义封装:
// 把 SPEC §7 表格里的中文事件名字翻成一次三/四通道调用,调用点(boat/fish/bucket)
// 不用逐个记 SND_*/HAPTIC_*/LED_FX_* 的具体取值。
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void feedback_bait_splash(void);     // 饵入水:波纹 + 噗通
void feedback_fish_notice(void);     // 鱼发现饵:瞪眼 + "哦?" + 灯带向鱼色渐亮
void feedback_bite(void);            // 咬钩:咬合猛帧 + 咔嗯! + BUMP_HARD + 灯带闪一次
void feedback_reel_tick(void);       // 收线中每次曲柄格:棘轮声 + tick(密度由调用频率天然体现)
void feedback_doze(void);            // 停手打盹:轻鼾一声(节流由调用方控制,只在刚进入时调一次)
void feedback_giggle(void);          // 没咬到:轻快咯咯(非失败音色)
void feedback_surface(void);         // 出水:水花 + 上扬滑音 + COLLECT + 白闪
void feedback_bucket_add(void);      // 进桶:噗通+叮 + BUMP_LIGHT + LED_FX_COLLECT
void feedback_party(void);           // 放生派对:SND_WIN + 上行琶音 + HAPTIC_WIN + LED_FX_WIN

#ifdef __cplusplus
}
#endif
