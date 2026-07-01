// bsp_imu.c —— 6 轴 IMU MPU6886(内部 I2C 0x68)
//
// ⚠️ Core2 v1.0 是 MPU6886(v1.3 才是 BMI270),靠 WHO_AM_I=0x19 区分(Core2 §7)。
// 初始化序列复刻 M5 官方 MPU6886.cpp;量程 ±8g(甩动会超 2g,留余量)。
#include "bsp.h"
#include "bsp_priv.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "bsp_imu";

#define WHOAMI_EXPECT     0x19
#define REG_WHOAMI        0x75
#define REG_SMPLRT_DIV    0x19
#define REG_CONFIG        0x1A
#define REG_GYRO_CONFIG   0x1B
#define REG_ACCEL_CONFIG  0x1C
#define REG_ACCEL_CONFIG2 0x1D
#define REG_FIFO_EN       0x23
#define REG_INT_PIN_CFG   0x37
#define REG_INT_ENABLE    0x38
#define REG_ACCEL_XOUT_H  0x3B
#define REG_USER_CTRL     0x6A
#define REG_PWR_MGMT_1    0x6B

#define ARES_8G  (8.0f / 32768.0f)

static i2c_master_dev_handle_t s_dev;
static uint8_t s_who = 0;

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

esp_err_t bsp_imu_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BSP_ADDR_MPU6886,
        .scl_speed_hz    = BSP_I2C_INTERNAL_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG, "add device");
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, BSP_ADDR_MPU6886, 100), TAG,
                        "MPU6886 未应答(0x68)");

    ESP_RETURN_ON_ERROR(reg_read(REG_WHOAMI, &s_who), TAG, "who_am_i");
    if (s_who != WHOAMI_EXPECT) {
        ESP_LOGW(TAG, "WHO_AM_I=0x%02X(期望 0x%02X)——可能不是 MPU6886!", s_who, WHOAMI_EXPECT);
    } else {
        ESP_LOGI(TAG, "WHO_AM_I=0x%02X 确认为 MPU6886", s_who);
    }

    // 复刻 M5 MPU6886::Init():复位 → PLL 时钟 → 量程/带宽
    ESP_RETURN_ON_ERROR(reg_write(REG_PWR_MGMT_1, 0x00), TAG, "pwr wake");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(reg_write(REG_PWR_MGMT_1, 0x80), TAG, "pwr reset");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(reg_write(REG_PWR_MGMT_1, 0x01), TAG, "pwr pll");
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_RETURN_ON_ERROR(reg_write(REG_ACCEL_CONFIG,  0x10), TAG, "accel ±8g");
    ESP_RETURN_ON_ERROR(reg_write(REG_GYRO_CONFIG,   0x18), TAG, "gyro 2000dps");
    ESP_RETURN_ON_ERROR(reg_write(REG_CONFIG,        0x01), TAG, "dlpf");
    ESP_RETURN_ON_ERROR(reg_write(REG_SMPLRT_DIV,    0x01), TAG, "smplrt");
    ESP_RETURN_ON_ERROR(reg_write(REG_INT_ENABLE,    0x00), TAG, "int dis");
    ESP_RETURN_ON_ERROR(reg_write(REG_ACCEL_CONFIG2, 0x00), TAG, "accel2");
    ESP_RETURN_ON_ERROR(reg_write(REG_USER_CTRL,     0x00), TAG, "user ctrl");
    ESP_RETURN_ON_ERROR(reg_write(REG_FIFO_EN,       0x00), TAG, "fifo");
    ESP_RETURN_ON_ERROR(reg_write(REG_INT_PIN_CFG,   0x22), TAG, "int pin");
    ESP_RETURN_ON_ERROR(reg_write(REG_INT_ENABLE,    0x01), TAG, "int en");
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "MPU6886 初始化完成(±8g, 内部 500Hz)");
    return ESP_OK;
}

esp_err_t bsp_imu_read_accel(float out_g[3])
{
    uint8_t reg = REG_ACCEL_XOUT_H;
    uint8_t buf[6];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, buf, 6, 100),
                        TAG, "read accel");
    out_g[0] = (int16_t)((buf[0] << 8) | buf[1]) * ARES_8G;
    out_g[1] = (int16_t)((buf[2] << 8) | buf[3]) * ARES_8G;
    out_g[2] = (int16_t)((buf[4] << 8) | buf[5]) * ARES_8G;
    return ESP_OK;
}

uint8_t bsp_imu_who_am_i(void)
{
    return s_who;
}
