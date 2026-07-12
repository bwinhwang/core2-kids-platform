// 反馈编排器(CLAUDE.md §6,仿 tilt_maze feedback.c 形状)。
// game_task 只发事件(非阻塞),实际分发在后台任务里做,不阻塞游戏循环。
//
// 全量事件(SPEC §6 反馈矩阵):bump(撞墙)/bounce(进错家弹出)/collect(归家)/
// party(全归家)/hello(ATTRACT 睡醒)/shake(摇一摇彩蛋)。
#pragma once

#include "esp_err.h"

/** @brief 起反馈后台任务 + 队列。需在 audio_fx 组件 init 之后调。 */
esp_err_t feedback_init(void);

/** @brief 第 idx 只动物撞上栅栏/灌木/家外墙且速度超阈(BUMP_MIN_SPEED):
 *         屏幕挤压脉冲(critters_squash,必做不节流)+ 极轻 boing(音频,全群共用节流窗口
 *         防机枪音)。触觉/灯带本事件均为"—"(SPEC §6),不发。 */
void feedback_emit_bump(int idx);

/** @brief 第 idx 只进错家被温柔弹出(SPEC §5.2/§6):摇头动画 + 轻"啵"(中性,不低沉)
 *         + HAPTIC_BUMP_LIGHT。同一只的节流(BOUNCE_SND_COOLDOWN_MS)由 game_state 做。 */
void feedback_emit_bounce(int idx);

/** @brief 一只归家(SPEC §5.3):叽/嘎短鸣(kind 定音色)+ 五声音阶第 total 音
 *         (total=已归家总数 1..ANIMAL_COUNT,进度天然可听)+ HAPTIC_COLLECT +
 *         LED_FX_COLLECT;total ≥ HOME_STRETCH_COUNT 灯带基色加亮一档(冲刺感)。
 *         捕获动画(critters_capture)由 game_state 直接起,不经本队列。 */
void feedback_emit_collect(int kind, int total);

/** @brief 全归家派对(SPEC §6):SND_WIN + HAPTIC_WIN + 灯带彩虹 + 两家弹跳 + 限量彩纸;
 *         结束把灯带基色收回 AMBIENT(顺带清掉冲刺加亮)。 */
void feedback_emit_party(void);

/** @brief ATTRACT 睡醒(SPEC §6):叽×2嘎×1 合唱 + HAPTIC_HELLO + 灯带 AMBIENT。
 *         全体小跳视觉由 game_state 直接调 critters_hop_all()。 */
void feedback_emit_hello(void);

/** @brief 摇一摇彩蛋(SPEC §6):叽嘎合唱 + HAPTIC_BUMP_MED + 灯带彩虹一闪;不改进度。 */
void feedback_emit_shake(void);
