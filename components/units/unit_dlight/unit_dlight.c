#include "unit_dlight.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "unit_dlight";

#define CMD_POWER_ON    0x01     // 上电(退出掉电态)
#define CMD_CONT_HRES   0x10     // 连续高分辨率模式(1lx 分辨率,测量周期 ~120ms)
#define LX_PER_COUNT    (1.0f / 1.2f)   // 高分辨率模式:lx = raw / 1.2
#define FIRST_MEAS_MS   180      // 选模式后等首次测量完成(> 120ms)
#define I2C_SPEED_HZ    100000   // 100kHz 保守稳妥(数据量极小)
#define I2C_TIMEOUT_MS  50

static i2c_master_dev_handle_t s_dev;

static esp_err_t write_cmd(uint8_t cmd)
{
    return i2c_master_transmit(s_dev, &cmd, 1, I2C_TIMEOUT_MS);
}

esp_err_t unit_dlight_init(i2c_master_bus_handle_t bus, uint8_t addr)
{
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (addr == 0) addr = UNIT_DLIGHT_ADDR_DEFAULT;

    if (!s_dev) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = addr,
            .scl_speed_hz    = I2C_SPEED_HZ,
        };
        esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2c_master_bus_add_device 失败: %s", esp_err_to_name(err));
            return err;
        }
    }

    // 上电 + 选连续高分辨率模式(每次都发:掉电复位后重调即可重新配好)。
    // 第一笔 transmit 兼作在位探测:没插/没电/插错口时这里失败。
    esp_err_t err = write_cmd(CMD_POWER_ON);
    if (err == ESP_OK) err = write_cmd(CMD_CONT_HRES);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DLight 无响应(%s):%s", esp_err_to_name(err),
                 err == ESP_ERR_INVALID_RESPONSE
                     ? "0x23 无应答——插错口(要插红色 PORT.A)?"
                     : "总线异常——线缆/供电(M-Bus 5V/EXTEN)?");
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(FIRST_MEAS_MS));   // 等首次测量完成,避免首读拿到旧值
    ESP_LOGI(TAG, "DLight 就绪 @0x%02X(连续高分辨率)", addr);
    return ESP_OK;
}

esp_err_t unit_dlight_read_lux(float *lux)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    if (!lux)   return ESP_ERR_INVALID_ARG;

    uint8_t b[2];
    // 连续模式:纯读 2 字节(无需先写命令)。读失败如实返回 → 调用方判"单元丢失"。
    esp_err_t err = i2c_master_receive(s_dev, b, 2, I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;

    uint16_t raw = ((uint16_t)b[0] << 8) | (uint16_t)b[1];   // 16-bit 大端
    *lux = (float)raw * LX_PER_COUNT;
    return ESP_OK;
}
