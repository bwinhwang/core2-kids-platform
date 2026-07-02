// 触觉:震动马达(AXP192 LDO3,经 BSP BSP_FEATURE_VIBRATION)—— CLAUDE.md §11
//
// 震动在独立任务里跑,play 只投队列,绝不阻塞 game_task。
// 提供家长总开关(有的家庭夜间不要震动)。
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAPTIC_HELLO = 0,   // 开机"你好":一下轻震
    HAPTIC_WAKE,        // 唤醒:一下轻震
    HAPTIC_BUMP_LIGHT,  // 撞墙-轻 ~30ms
    HAPTIC_BUMP_MED,    // 撞墙-中 ~60ms
    HAPTIC_BUMP_HARD,   // 撞墙-重 ~100ms
    HAPTIC_COLLECT,     // 收集星:极短 ~25ms
    HAPTIC_WIN,         // 过关:欢庆三连震
    HAPTIC_PATTERN_MAX,
} haptic_pattern_t;

/** @brief 起震动后台任务(BSP 已管 LDO3 供电,无需额外初始化电源)。 */
esp_err_t haptics_init(void);

/** @brief 非阻塞触发一个震动模式(投入队列,后台执行)。 */
void haptics_play(haptic_pattern_t pattern);

/** @brief 家长总开关。关闭后 play 被忽略。默认开。 */
void haptics_set_enabled(bool enabled);

#ifdef __cplusplus
}
#endif
