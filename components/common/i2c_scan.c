#include "i2c_scan.h"
#include "esp_log.h"

static const char *TAG = "i2c_scan";

void i2c_scan(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "扫描 I2C 总线(0x08~0x77)...");
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(bus, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "  发现设备 @ 0x%02X", addr);
            found++;
        }
    }
    ESP_LOGI(TAG, "扫描完成,共 %d 个设备", found);
}
