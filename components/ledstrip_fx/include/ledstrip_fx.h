// 底座灯带（M5GO Bottom2: 10× SK6812 @ G25，常驻反馈通道）—— CLAUDE.md §12
//
// ⚠️ 叠底座后 G25 被灯条占用，不能再当 DAC 用。亮度压低(§13)。
// 内部动画任务常驻刷新:基础模式(氛围) + 瞬态特效(撞墙/收集/过关),异步,不阻塞 game_task。
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LEDSTRIP_GPIO       25
#define LEDSTRIP_COUNT      10

// 基础(常态)模式
typedef enum {
    LED_BASE_AMBIENT = 0,   // 游戏中:暖色微光
    LED_BASE_NEAR,          // 接近目标:加亮向目标色
    LED_BASE_IDLE,          // 打盹:极低极慢呼吸
    LED_BASE_OFF,           // 深度省电:全熄
} led_base_t;

// 瞬态特效(放完自动回到基础模式)
typedef enum {
    LED_FX_BUMP = 0,        // 撞墙:一下微闪
    LED_FX_COLLECT,         // 收集星:一圈高亮扫过
    LED_FX_WIN,             // 过关:彩虹转圈
    // ── 以下为 busy_knobs 图案彩蛋追加(尾部追加,不改动既有值序)──────────
    LED_FX_SWEEP_L2R,       // 定向扫:COLLECT 同款遍历序正向
    LED_FX_SWEEP_R2L,       // 定向扫:反向
    LED_FX_GATHER,          // 两端 → 中间聚拢
    LED_FX_SPREAD,          // 中间 → 两端散开
    LED_FX_FLASH,           // 整条暖白柔亮一下(≈250ms 起落,无频闪感)
} led_fx_t;

/** @brief 初始化灯带 + 起动画任务。 */
esp_err_t ledstrip_fx_init(void);

/** @brief 设全局亮度上限(0~255),所有颜色按此缩放。默认 48。 */
void ledstrip_fx_set_max_brightness(uint8_t max);

/** @brief 设基础(常态)模式。 */
void ledstrip_fx_set_base(led_base_t base);

/** @brief 触发一次瞬态特效(非阻塞)。 */
void ledstrip_fx_trigger(led_fx_t fx);

#ifdef __cplusplus
}
#endif
