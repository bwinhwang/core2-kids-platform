// MPU6886 最小驱动 —— 只为"读重力倾斜"服务（CLAUDE.md §8.1）
//
// ⚠️ 本机 IMU 是 MPU6886（Core2 v1.0 / 经 Bottom2 提供，I2C 0x68），
//    寄存器图与 BMI270 完全不同，切勿照搬 BMI270 代码（Core2_v1_0.md §7）。
//
// 设计：复用 BSP 已初始化的内部 I2C 总线（bsp_i2c_get_handle()），
//      不自己再 init 一条总线（会和 AXP192/触摸抢）。
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// MPU6886 WHO_AM_I 寄存器的期望返回值（datasheet）
#define MPU6886_WHO_AM_I_VALUE  0x19

typedef struct {
    float x;  // 加速度 X（g）
    float y;  // 加速度 Y（g）
    float z;  // 加速度 Z（g）
} imu_accel_t;

/**
 * @brief 在已初始化的 I2C 主总线上挂载并初始化 MPU6886。
 * @param bus  来自 bsp_i2c_get_handle() 的内部 I2C 总线句柄。
 * @return ESP_OK 成功；ESP_ERR_NOT_FOUND 即 WHO_AM_I 不符（可能没接底座/IMU）。
 */
esp_err_t imu_mpu6886_init(i2c_master_bus_handle_t bus);

/** @brief 读三轴加速度（单位 g，±2g 量程，16384 LSB/g）。 */
esp_err_t imu_mpu6886_read_accel(imu_accel_t *out);

/** @brief 读 WHO_AM_I 寄存器原值（用于 M0 自检，应为 0x19）。 */
esp_err_t imu_mpu6886_whoami(uint8_t *who);

#ifdef __cplusplus
}
#endif
