// 倾斜物理 (CLAUDE.md §8.3)。只吃"倾斜向量",不碰 I2C 寄存器(§3.1 分层)。
#pragma once

#include <stdbool.h>
#include "imu_mpu6886.h"

typedef struct { float x, y; } vec2_t;

typedef struct {
    vec2_t pos;          // 球心(px)
    vec2_t vel;          // 速度(px/s)
    imu_accel_t a_filt;  // 低通后的加速度(g)
    vec2_t zero;         // 校准零点(已映射到屏幕轴, g)
    bool   bumped;       // 本帧是否撞边
    float  bump_speed;   // 撞击强度(法向速度, px/s)
} physics_t;

/** @brief 初始化:球放在 (start_x,start_y),速度 0。 */
void physics_init(physics_t *p, float start_x, float start_y);

/** @brief 用一段静止平均读数设中性零点(§8.2);同时预热低通滤波。 */
void physics_calibrate(physics_t *p, const imu_accel_t *avg_raw);

/** @brief 只把球放到 (x,y) 并清零速度;保留校准零点与滤波状态(跨关复用)。 */
void physics_set_position(physics_t *p, float x, float y);

/** @brief 推进一帧:读本帧 IMU 原始加速度(g),走完滤波/死区/积分/撞边。 */
void physics_step(physics_t *p, const imu_accel_t *accel_raw, float dt);
