#include "unit_ultrasonic.h"

#include "esp_log.h"

static const char *TAG = "unit_ultrasonic";

#define CMD_TRIGGER     0x01     // 触发一次测量(写单字节)
#define I2C_SPEED_HZ    100000   // 100kHz 保守稳妥(数据量极小)
#define I2C_TIMEOUT_MS  50

static i2c_master_dev_handle_t s_dev;

esp_err_t unit_ultrasonic_init(i2c_master_bus_handle_t bus, uint8_t addr)
{
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (addr == 0) addr = UNIT_ULTRASONIC_ADDR_DEFAULT;

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

    // 在位检查:发一次触发,看 0x57 是否 ACK(没插/没电时这里失败——注意 PORT.A 5V 靠 EXTEN)
    esp_err_t err = unit_ultrasonic_trigger();
    if (err != ESP_OK) {
        // INVALID_RESPONSE=NACK(总线通但 0x57 无应答 → 多半插错口:红色 PORT.A 在机身侧面);
        // TIMEOUT=总线拉死(线缆/供电/上拉)。用 core2_board_port_a_scan() 一扫便知。
        ESP_LOGW(TAG, "超声波无响应(%s):%s", esp_err_to_name(err),
                 err == ESP_ERR_INVALID_RESPONSE
                     ? "0x57 无应答——插错口(要插红色 PORT.A)?"
                     : "总线异常——线缆/供电(M-Bus 5V/EXTEN)?");
        return err;
    }

    ESP_LOGI(TAG, "超声波就绪 @0x%02X", addr);
    return ESP_OK;
}

esp_err_t unit_ultrasonic_trigger(void)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t cmd = CMD_TRIGGER;
    return i2c_master_transmit(s_dev, &cmd, 1, I2C_TIMEOUT_MS);
}

esp_err_t unit_ultrasonic_read_mm(float *mm)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    if (!mm)    return ESP_ERR_INVALID_ARG;

    uint8_t b[3];
    // 读事务本身失败(TIMEOUT=总线拉死 / INVALID_RESPONSE=NACK)如实返回 → 调用方判"单元喪失"
    esp_err_t err = i2c_master_receive(s_dev, b, 3, I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;

    // 24-bit 大端(b[0]=高字节),单位微米 → 毫米
    uint32_t raw = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | (uint32_t)b[2];
    float dist = (float)raw / 1000.0f;

    // 收到了数据但越界 = 单元活着、只是没有目标(手离开量程)。用 NOT_FOUND 与上面的
    // 通信错误区分开:调用方据此判"没目标"而**不**当作拔线。
    if (dist < UNIT_ULTRASONIC_MIN_MM || dist > UNIT_ULTRASONIC_MAX_MM) {
        return ESP_ERR_NOT_FOUND;
    }
    *mm = dist;
    return ESP_OK;
}
