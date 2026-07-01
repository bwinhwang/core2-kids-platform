// engine.h —— 默认交互 engine:晃动信号处理(纯算法,无硬件依赖)
//
// 这是【可替换的默认 demo】。输入三轴加速度(g),输出连续强度 i∈[0,1] +
// 离散单次甩动 peak。用慢 EMA 跟踪重力并扣除 → 天然方向无关,无需开机静止校准。
// 换应用时:若输入不是 IMU 而是某个 UNIT,把本文件替换成对应 engine 即可。
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float    g_est[3];       // 重力分量估计(慢 EMA)
    float    env;            // 强度包络(快攻慢放)
    float    prev_energy;    // 上一帧能量(peak 上升沿检测)
    uint32_t last_peak_ms;   // 最近一次 peak 时间戳
    bool     primed;         // 是否已用首帧初始化 g_est
} shake_engine_t;

typedef struct {
    float intensity;   // i ∈ [0,1]
    bool  peak;        // 本次是否触发单次甩动事件
    float peak_mag;    // 若 peak,触发时的能量(g)
} shake_result_t;

void shake_engine_init(shake_engine_t *e);
shake_result_t shake_engine_update(shake_engine_t *e, const float accel_g[3], uint32_t now_ms);

#ifdef __cplusplus
}
#endif
