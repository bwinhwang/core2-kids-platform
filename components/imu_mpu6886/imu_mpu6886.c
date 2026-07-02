#include "imu_mpu6886.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "imu_mpu6886";

// ── MPU6886 寄存器图（datasheet，勿与 BMI270 混淆）────────────────────
#define MPU6886_ADDR            0x68
#define REG_SMPLRT_DIV          0x19
#define REG_CONFIG              0x1A
#define REG_GYRO_CONFIG         0x1B
#define REG_ACCEL_CONFIG        0x1C   // bit[4:3] = AFS_SEL (0=±2g)
#define REG_ACCEL_CONFIG2       0x1D   // accel DLPF
#define REG_INT_PIN_CFG         0x37
#define REG_INT_ENABLE          0x38
#define REG_ACCEL_XOUT_H        0x3B   // 6 字节大端: X/Y/Z 各 16bit
#define REG_PWR_MGMT_1          0x6B
#define REG_PWR_MGMT_2          0x6C
#define REG_WHO_AM_I            0x75

// ±2g 量程下的灵敏度：16384 LSB/g
#define ACCEL_SENS_2G           16384.0f

#define I2C_TIMEOUT_MS          100

static i2c_master_dev_handle_t s_dev;   // 0x68 设备句柄

// ── 寄存器读写小工具 ─────────────────────────────────────────────────
static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, I2C_TIMEOUT_MS);
}

esp_err_t imu_mpu6886_whoami(uint8_t *who)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    return reg_read(REG_WHO_AM_I, who, 1);
}

esp_err_t imu_mpu6886_init(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = MPU6886_ADDR,
        .scl_speed_hz    = 400000,   // MPU6886 支持 400kHz 快速模式
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device 失败: %s", esp_err_to_name(err));
        return err;
    }

    // 1) 校验芯片型号：确认是 MPU6886，不是 BMI270 或缺底座
    uint8_t who = 0;
    err = reg_read(REG_WHO_AM_I, &who, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "读 WHO_AM_I 失败: %s（底座没接？IMU 不在 0x68？）", esp_err_to_name(err));
        return err;
    }
    if (who != MPU6886_WHO_AM_I_VALUE) {
        ESP_LOGE(TAG, "WHO_AM_I=0x%02X，期望 0x%02X —— 这不是 MPU6886（BMI270 会返回别的值）",
                 who, MPU6886_WHO_AM_I_VALUE);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "检测到 MPU6886 (WHO_AM_I=0x%02X)", who);

    // 2) 复位后唤醒并配置（M5Stack MPU6886 标准序列）
    ESP_RETURN_ON_ERROR(reg_write(REG_PWR_MGMT_1, 0x80), TAG, "PWR_MGMT_1 reset");  // device reset
    vTaskDelay(pdMS_TO_TICKS(15));
    ESP_RETURN_ON_ERROR(reg_write(REG_PWR_MGMT_1, 0x01), TAG, "PWR_MGMT_1 clk");    // 自动选时钟源
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(reg_write(REG_PWR_MGMT_2, 0x00), TAG, "PWR_MGMT_2");        // 加速度/陀螺全开
    ESP_RETURN_ON_ERROR(reg_write(REG_ACCEL_CONFIG, 0x00), TAG, "ACCEL_CONFIG");    // ±2g（最高分辨率，测倾斜够用）
    ESP_RETURN_ON_ERROR(reg_write(REG_GYRO_CONFIG, 0x18), TAG, "GYRO_CONFIG");      // ±2000dps（本应用不用陀螺）
    ESP_RETURN_ON_ERROR(reg_write(REG_CONFIG, 0x01), TAG, "CONFIG");                // DLPF ~176Hz
    ESP_RETURN_ON_ERROR(reg_write(REG_ACCEL_CONFIG2, 0x00), TAG, "ACCEL_CONFIG2");  // accel DLPF ~218Hz
    ESP_RETURN_ON_ERROR(reg_write(REG_SMPLRT_DIV, 0x04), TAG, "SMPLRT_DIV");        // 采样率分频
    ESP_RETURN_ON_ERROR(reg_write(REG_INT_PIN_CFG, 0x00), TAG, "INT_PIN_CFG");
    ESP_RETURN_ON_ERROR(reg_write(REG_INT_ENABLE, 0x00), TAG, "INT_ENABLE");        // 不用中断（轮询读）
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "MPU6886 初始化完成（±2g, %.0f LSB/g）", ACCEL_SENS_2G);
    return ESP_OK;
}

esp_err_t imu_mpu6886_read_accel(imu_accel_t *out)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    if (!out)   return ESP_ERR_INVALID_ARG;

    uint8_t raw[6];
    esp_err_t err = reg_read(REG_ACCEL_XOUT_H, raw, sizeof(raw));
    if (err != ESP_OK) return err;

    // 大端 high/low，有符号 16bit
    int16_t ax = (int16_t)((raw[0] << 8) | raw[1]);
    int16_t ay = (int16_t)((raw[2] << 8) | raw[3]);
    int16_t az = (int16_t)((raw[4] << 8) | raw[5]);

    out->x = ax / ACCEL_SENS_2G;
    out->y = ay / ACCEL_SENS_2G;
    out->z = az / ACCEL_SENS_2G;
    return ESP_OK;
}
