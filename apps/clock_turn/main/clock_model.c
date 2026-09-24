#include "clock_model.h"

int clock_model_wrap(int t)
{
    int m = t % CLOCK_MODEL_T_PERIOD;
    if (m < 0) {
        m += CLOCK_MODEL_T_PERIOD;
    }
    return m;
}

int clock_model_step(int t, int delta_steps, int min_per_step)
{
    return clock_model_wrap(t + delta_steps * min_per_step);
}

float clock_model_hour_angle(int t)
{
    return (float)t * 0.5f;
}

float clock_model_minute_angle(int t)
{
    return (float)(t % 60) * 6.0f;
}

bool clock_model_is_exact(int t)
{
    return (t % 30) == 0;
}

bool clock_model_is_quarter(int t)
{
    return (t % 15) == 0;
}

int clock_model_next_quiz(int current_t, int last_target, int grain_min, uint32_t rand_u32)
{
    if (grain_min <= 0) {
        grain_min = 1;
    }
    int pool_n = CLOCK_MODEL_T_PERIOD / grain_min;   // grain_min=15 → 48
    if (pool_n <= 0) {
        pool_n = 1;
    }

    int idx = (int)(rand_u32 % (uint32_t)pool_n);
    for (int tries = 0; tries < pool_n; tries++) {
        int cand = clock_model_wrap(idx * grain_min);
        if (cand != current_t && cand != last_target) {
            return cand;
        }
        idx = (idx + 1) % pool_n;   // 撞了(=当前/上一题)就线性找下一个,保证终止
    }
    // 理论上走不到这里(pool_n>=2 时至多排掉 2 个候选);兜底返回起点候选。
    return clock_model_wrap(idx * grain_min);
}

int clock_model_snap(int t, int min_per_step)
{
    t = clock_model_wrap(t);
    return clock_model_wrap((t + min_per_step / 2) / min_per_step * min_per_step);
}
