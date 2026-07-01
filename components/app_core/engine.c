#include "engine.h"
#include "app_tuning.h"
#include <math.h>

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void shake_engine_init(shake_engine_t *e)
{
    e->g_est[0] = e->g_est[1] = e->g_est[2] = 0.0f;
    e->env = 0.0f;
    e->prev_energy = 0.0f;
    e->last_peak_ms = 0;
    e->primed = false;
}

shake_result_t shake_engine_update(shake_engine_t *e, const float a[3], uint32_t now_ms)
{
    shake_result_t r = { .intensity = 0.0f, .peak = false, .peak_mag = 0.0f };

    // 首帧:把当前读数当作重力初值,避免开机瞬间的假能量
    if (!e->primed) {
        e->g_est[0] = a[0]; e->g_est[1] = a[1]; e->g_est[2] = a[2];
        e->primed = true;
        return r;
    }

    // 1) 慢 EMA 跟踪重力(自动扣除 → 方向无关)
    for (int i = 0; i < 3; i++) {
        e->g_est[i] = (1.0f - GRAVITY_EMA_A) * e->g_est[i] + GRAVITY_EMA_A * a[i];
    }

    // 2) 动态能量 = |a - g_est|
    float dx = a[0] - e->g_est[0], dy = a[1] - e->g_est[1], dz = a[2] - e->g_est[2];
    float energy = sqrtf(dx * dx + dy * dy + dz * dz);

    // 3) 包络:快攻慢放(把一串甩动合成稳定包络)
    e->env = (energy > e->env) ? energy : e->env * ENV_RELEASE;

    // 4) 归一化 → 死区 + 饱和 + gamma 低端提升
    float i_raw = clampf((e->env - ENERGY_FLOOR_G) / (ENERGY_MAX_G - ENERGY_FLOOR_G), 0.0f, 1.0f);
    r.intensity = powf(i_raw, INTENSITY_GAMMA);

    // 5) 单次甩动 peak:能量上升沿越过门限 + 不应期
    if (energy > PEAK_THRESH_G && e->prev_energy <= PEAK_THRESH_G &&
        (now_ms - e->last_peak_ms) > PEAK_REFRACT_MS) {
        r.peak = true;
        r.peak_mag = energy;
        e->last_peak_ms = now_ms;
    }
    e->prev_energy = energy;
    return r;
}
