// kids_safety.h —— 面向幼儿的产品【红线】(整条产品线通用,不要按单个应用随意放宽)
//
// 三条铁律:①音量封顶(听力保护)②亮度封顶(护眼防刺激)③低电仍可玩(不锁死)。
// 另加“渐入防惊吓”:输出用斜坡逼近目标,避免突然的强光/巨响吓到幼儿。
// 所有输出(灯/声)在写硬件前都应过一遍这里的限幅。
#pragma once

#include <stdint.h>

// 音量上限:0~1。⚠️ 上板用声级计 @25cm 实测后定(建议 ≲75dBA)
#define KIDS_MAX_VOLUME       0.5f
// 亮度上限:0~255。SK6812 满亮度刺眼且耗流
#define KIDS_MAX_BRIGHTNESS   96
// 低电压阈值(V):仅叠加提示(如琥珀脉冲),【始终可玩】,不进入锁定
#define KIDS_LOWBAT_VOLT      3.30f
// 渐入斜坡:每步向目标逼近的比例(防突变惊吓)
#define KIDS_SLEW_PER_STEP    0.10f

static inline float kids_clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// 单位强度(0~1)→ 封顶后的灯珠亮度(0~KIDS_MAX_BRIGHTNESS)
static inline uint8_t kids_bright(float unit01)
{
    return (uint8_t)(kids_clamp01(unit01) * KIDS_MAX_BRIGHTNESS);
}

// 单位强度(0~1)→ 封顶后的音量系数(0~KIDS_MAX_VOLUME)
static inline float kids_volume(float unit01)
{
    return kids_clamp01(unit01) * KIDS_MAX_VOLUME;
}
