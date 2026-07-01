#include "unit_template.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "unit_template";
static i2c_master_dev_handle_t s_dev;

esp_err_t unit_template_init(i2c_master_bus_handle_t bus, uint8_t addr)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = 100000,      // Grove I2C UNIT 多为 100k
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add device");
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, addr, 100), TAG, "UNIT 未应答 @0x%02X", addr);
    ESP_LOGI(TAG, "UNIT 就绪 @0x%02X", addr);
    return ESP_OK;
}

esp_err_t unit_template_read(uint16_t *value)
{
    // 示例:从寄存器 0x00 读 2 字节大端。按具体 UNIT 文档改寄存器/字节序/单位换算。
    uint8_t reg = 0x00, buf[2];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, buf, 2, 100), TAG, "read");
    *value = (uint16_t)((buf[0] << 8) | buf[1]);
    return ESP_OK;
}
