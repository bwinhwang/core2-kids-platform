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
    p->filt_init = false;
    p->bumped = false;
    p->bump_speed = 0;
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
    // 1) 低通滤波(§8.3 step2);首帧直接用样本预热,避免从 0 收敛的开局跳变
    if (!p->filt_init) {
        p->a_filt = *accel_raw;
        p->filt_init = true;
    }
    const float a = TILT_ALPHA;
    p->a_filt.x = p->a_filt.x * (1 - a) + accel_raw->x * a;
    p->a_filt.y = p->a_filt.y * (1 - a) + accel_raw->y * a;
    p->a_filt.z = p->a_filt.z * (1 - a) + accel_raw->z * a;

    // 2) 映射到屏幕轴 → 倾斜向量。绝对零点(§20.9):x/y 的重力分量就是倾斜,
    //    "平"= 与地面平,无校准、无隐藏参考系;板偏置由死区吸收(实测 <0.03g)。
    vec2_t m = map_axes(&p->a_filt);
    float tx = m.x;
    float ty = m.y;

    // 3) 死区(§8.3 step4,减除式):防平放时球自己飘。
    //    只把"超出死区的部分"交给增益——硬截断会造成悬崖:零点稍被污染(>死区)
    //    就吃全额恒力,球以 ~20px/s 匀速单向爬(§20.8 实测坑)。减除式下同样的
    //    残余偏差只剩(残余-死区)的小力,阻尼便能压住。
    float tmag = sqrtf(tx * tx + ty * ty);
    if (tmag < TILT_DEADZONE) {
        tx = 0;
        ty = 0;
    } else {
        float k = (tmag - TILT_DEADZONE) / tmag;
        tx *= k;
        ty *= k;
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
