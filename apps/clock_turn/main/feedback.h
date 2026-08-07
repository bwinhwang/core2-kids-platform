// feedback —— 事件 → 音效/震动/灯带/Chain RGB 编排(仿 chick_pour/tilt_maze feedback.c 形状)
//
// game_task 只发事件(非阻塞,xQueueSend 满了丢弃),实际分发在后台任务里做,不阻塞游戏循环。
// 2026-08-06 语音整章作废后,本卡带的"声音"只剩 audio_fx 程序化合成这一条路(SPEC §7/§8)。
//
// 全量事件(SPEC §7 反馈矩阵):转一格(含整齐时刻加重)/ MODE_FREE 按键揭晓 /
// MODE_QUIZ 答对 / MODE_QUIZ 还没到 / 模式切换成功。"旋钮拔线"不经本队列(纯 UI 状态,
// 由 main.c 直接调 clock_ui_set_linked)。
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 转一格落在哪一类位置上(SPEC §5.2 2026-08-06 判断题:落格反馈用 t%15==0,比教学
// 叙事的 t%30==0 更细,三个"整齐"子类共享同一套"咔哒更亮+BUMP_LIGHT+LED微亮",只有
// Chain RGB 颜色不同,用来区分是哪一类)。
typedef enum {
    FEEDBACK_TICK_PLAIN = 0,   // 普通一格:极轻咔哒,RGB 回落到暖色基线
    FEEDBACK_TICK_QUARTER,     // 一刻(:15/:45):咔哒更亮,RGB 暗黄
    FEEDBACK_TICK_HALF,        // 半点(:30):咔哒更亮,RGB 蓝
    FEEDBACK_TICK_HOUR,        // 整点(:00):咔哒更亮,RGB 绿
} feedback_tick_kind_t;

/** @brief 起反馈后台任务 + 队列。需在 audio_fx/haptics/ledstrip_fx 组件 init 之后调。 */
esp_err_t feedback_init(void);

/** @brief 告诉 feedback 当前接管的 Chain Encoder 链位(拿去发 `chain_bus_set_rgb`)。
 *         id=0 = 未接管(此时 Chain RGB 相关调用自动跳过,不报错)。热插拔重接管时
 *         main.c 重新调一次即可。 */
void feedback_set_chain_id(uint8_t id);

/** @brief 转一格(SPEC §7 第一行 + 落在整齐时刻那一行合并处理,kind 决定轻重与 RGB 颜色)。 */
void feedback_emit_step(feedback_tick_kind_t kind);

/** @brief MODE_FREE 按键揭晓当前读数(SPEC §5.4):轻「叮」,其余通道均为"—"。 */
void feedback_emit_reveal(void);

/** @brief MODE_QUIZ 答对(SPEC §5.6/§6.3):上行琶音 + WIN 震动 + 彩虹迸发 + Chain RGB 彩虹一扫。
 *         画面庆祝(泛光/绿字/yay脸/12数字点亮)由 main.c 直接调 clock_ui_play_win() 处理,
 *         不经本队列。 */
void feedback_emit_win(void);

/** @brief MODE_QUIZ 按键但未到位(SPEC §5.6):两声软下行(不是刺耳错误音)+ BUMP_LIGHT +
 *         暖色柔亮一闪 + Chain RGB 暗黄一闪。 */
void feedback_emit_miss(void);

/** @brief 状态条①长按满 1.5s 切换模式成功(SPEC §5.3.5):双音 + BUMP_LIGHT。 */
void feedback_emit_mode_switch(void);

#ifdef __cplusplus
}
#endif
