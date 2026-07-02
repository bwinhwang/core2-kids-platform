#include "physics.h"
#include "tuning.h"
#include <math.h>

// 把 IMU 三轴(g)按 tuning 的交换/取反映射成屏幕平面 (x→右, y→下)
static vec2_t map_axes(const imu_accel_t *a)
{
#if TILT_SWAP_XY
    float sx = a->y, sy = a->x;
#else
    float sx = a->x, sy = a->y;
#endif
#if TILT_INVERT_X
    sx = -sx;
#endif
#if TILT_INVERT_Y
    sy = -sy;
#endif
    return (vec2_t){ sx, sy };
}

void physics_init(physics_t *p, float start_x, float start_y)
{
    p->pos = (vec2_t){ start_x, start_y };
    p->vel = (vec2_t){ 0, 0 };
    p->a_filt = (imu_accel_t){ 0, 0, 0 };
    p->zero = (vec2_t){ 0, 0 };
    p->bumped = false;
    p->bump_speed = 0;
}

void physics_calibrate(physics_t *p, const imu_accel_t *avg_raw)
{
    p->a_filt = *avg_raw;             // 预热滤波,避免开局跳变
    p->zero = map_axes(avg_raw);      // 这个握持姿态即"平"
}

void physics_set_position(physics_t *p, float x, float y)
{
    p->pos = (vec2_t){ x, y };
    p->vel = (vec2_t){ 0, 0 };
    p->bumped = false;
    p->bump_speed = 0;
}

void physics_step(physics_t *p, const imu_accel_t *accel_raw, float dt)
{
    // 1) 低通滤波(§8.3 step2)
    const float a = TILT_ALPHA;
    p->a_filt.x = p->a_filt.x * (1 - a) + accel_raw->x * a;
    p->a_filt.y = p->a_filt.y * (1 - a) + accel_raw->y * a;
    p->a_filt.z = p->a_filt.z * (1 - a) + accel_raw->z * a;

    // 2) 映射到屏幕轴并减去零点 → 倾斜向量
    vec2_t m = map_axes(&p->a_filt);
    float tx = m.x - p->zero.x;
    float ty = m.y - p->zero.y;

    // 3) 死区(§8.3 step4):防平放时球自己飘
    float tmag = sqrtf(tx * tx + ty * ty);
    if (tmag < TILT_DEADZONE) {
        tx = 0;
        ty = 0;
    }

    // 4) 倾斜→加速度→速度(强阻尼)→封顶(§8.3 step5~7)
    p->vel.x += tx * TILT_GAIN * dt;
    p->vel.y += ty * TILT_GAIN * dt;
    p->vel.x *= VEL_DAMPING;
    p->vel.y *= VEL_DAMPING;

    float sp = sqrtf(p->vel.x * p->vel.x + p->vel.y * p->vel.y);
    if (sp > VEL_MAX) {
        float k = VEL_MAX / sp;
        p->vel.x *= k;
        p->vel.y *= k;
    }

    // 5) 积分位置
    p->pos.x += p->vel.x * dt;
    p->pos.y += p->vel.y * dt;

    // 6) 撞屏幕四边(M2 边界=墙):钳位 + 法向回弹,记录撞击强度
    p->bumped = false;
    p->bump_speed = 0;
    const float minx = BALL_R, maxx = PLAY_W - BALL_R;
    const float miny = BALL_R, maxy = PLAY_H - BALL_R;

    if (p->pos.x < minx) {
        p->pos.x = minx;
        if (p->vel.x < 0) { p->bump_speed = fmaxf(p->bump_speed, -p->vel.x); p->vel.x = -p->vel.x * WALL_RESTITUTION; p->bumped = true; }
    } else if (p->pos.x > maxx) {
        p->pos.x = maxx;
        if (p->vel.x > 0) { p->bump_speed = fmaxf(p->bump_speed, p->vel.x); p->vel.x = -p->vel.x * WALL_RESTITUTION; p->bumped = true; }
    }
    if (p->pos.y < miny) {
        p->pos.y = miny;
        if (p->vel.y < 0) { p->bump_speed = fmaxf(p->bump_speed, -p->vel.y); p->vel.y = -p->vel.y * WALL_RESTITUTION; p->bumped = true; }
    } else if (p->pos.y > maxy) {
        p->pos.y = maxy;
        if (p->vel.y > 0) { p->bump_speed = fmaxf(p->bump_speed, p->vel.y); p->vel.y = -p->vel.y * WALL_RESTITUTION; p->bumped = true; }
    }
}
