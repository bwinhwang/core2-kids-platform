// bsp_power.c —— AXP192 电源管理(内部 I2C 0x34)
//
// ⚠️ 不初始化 AXP192 → 屏黑/没声/没触摸(Core2 §2)。寄存器序列严格复刻 M5Core2
// 官方 AXP192.cpp begin()(取自现成 M5 参考实现,不要凭印象改)。
#include "bsp.h"
#include "bsp_priv.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "bsp_power";
static i2c_master_dev_handle_t s_dev;

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

// M5 惯用法:Write(reg, (Read(reg) & keep) | set)
static esp_err_t reg_update(uint8_t reg, uint8_t keep, uint8_t set)
{
    uint8_t v;
    ESP_RETURN_ON_ERROR(reg_read(reg, &v), TAG, "read 0x%02X", reg);
    v = (uint8_t)((v & keep) | set);
    return reg_write(reg, v);
}

// 12-bit ADC:高 8 位在 reg,低 4 位在 reg+1 的 [3:0]
static esp_err_t reg_read12(uint8_t reg, uint16_t *out)
{
    uint8_t buf[2];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, buf, 2, 100), TAG, "adc");
    *out = (uint16_t)((buf[0] << 4) | (buf[1] & 0x0F));
    return ESP_OK;
}

// 选 M-Bus 5V 来源:0=内部升压输出 / 非0=外部供电
static esp_err_t set_bus_power_mode(uint8_t state)
{
    if (state == 0) {
        ESP_RETURN_ON_ERROR(reg_update(0x91, 0x0F, 0xF0), TAG, "0x91");
        ESP_RETURN_ON_ERROR(reg_update(0x90, 0xF8, 0x02), TAG, "0x90");
        ESP_RETURN_ON_ERROR(reg_update(0x10, 0xFF, 0x04), TAG, "0x10");
    } else {
        ESP_RETURN_ON_ERROR(reg_update(0x10, 0xFB, 0x00), TAG, "0x10");
        ESP_RETURN_ON_ERROR(reg_update(0x90, 0xF8, 0x07), TAG, "0x90");
    }
    return ESP_OK;
}

esp_err_t bsp_power_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BSP_ADDR_AXP192,
        .scl_speed_hz    = BSP_I2C_INTERNAL_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG, "add device");
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, BSP_ADDR_AXP192, 100), TAG,
                        "AXP192 未应答(0x34)——检查内部 I2C/上电");

    // 严格对应 M5Core2 AXP192::begin()
    ESP_RETURN_ON_ERROR(reg_update(0x30, 0x04, 0x02), TAG, "vbus limit");
    ESP_RETURN_ON_ERROR(reg_update(0x92, 0xF8, 0x00), TAG, "gpio1");
    ESP_RETURN_ON_ERROR(reg_update(0x93, 0xF8, 0x00), TAG, "gpio2 (SPK_EN)");
    ESP_RETURN_ON_ERROR(reg_update(0x35, 0x1C, 0xA2), TAG, "rtc chg");

    ESP_RETURN_ON_ERROR(reg_update(0x26, 0x80, 0x6A), TAG, "esp 3.35v");
    ESP_RETURN_ON_ERROR(reg_update(0x27, 0x80, 0x54), TAG, "lcd bl 2.8v");
    ESP_RETURN_ON_ERROR(reg_update(0x28, 0x0F, 0xF0), TAG, "ldo2 3.3v");
    ESP_RETURN_ON_ERROR(reg_update(0x28, 0xF0, 0x02), TAG, "ldo3 2.0v");

    ESP_RETURN_ON_ERROR(reg_update(0x12, 0xFF, (1 << 2) | (1 << 1)), TAG, "en ldo2+dcdc3");
    ESP_RETURN_ON_ERROR(reg_update(0x94, 0xFD, 0x00), TAG, "led on");
    ESP_RETURN_ON_ERROR(reg_update(0x33, 0xF0, 0x00), TAG, "chg 100mA");

    ESP_RETURN_ON_ERROR(reg_update(0x95, 0x72, 0x84), TAG, "gpio4");
    ESP_RETURN_ON_ERROR(reg_write(0x36, 0x4C), TAG, "pek");
    ESP_RETURN_ON_ERROR(reg_write(0x82, 0xFF), TAG, "adc en");

    // LCD/触摸复位脉冲(IO4 → reg 0x96 bit1)
    ESP_RETURN_ON_ERROR(reg_update(0x96, 0xFD, 0x00), TAG, "rst low");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(reg_update(0x96, 0xFF, 0x02), TAG, "rst high");
    vTaskDelay(pdMS_TO_TICKS(100));

    // 按 VBUS 是否插入决定 M-Bus 5V 方向
    uint8_t st = 0;
    ESP_RETURN_ON_ERROR(reg_read(0x00, &st), TAG, "read 0x00");
    if (st & 0x08) {
        ESP_RETURN_ON_ERROR(reg_update(0x30, 0xFF, 0x80), TAG, "vbus en");
        ESP_RETURN_ON_ERROR(set_bus_power_mode(1), TAG, "bus input");
    } else {
        ESP_RETURN_ON_ERROR(set_bus_power_mode(0), TAG, "bus output");
    }

#if !CONFIG_BSP_ENABLE_DISPLAY
    ESP_RETURN_ON_ERROR(reg_update(0x12, 0xFD, 0x00), TAG, "bl off");   // 不用屏:关背光省电
#endif
    ESP_RETURN_ON_ERROR(bsp_power_set_speaker(false), TAG, "spk off");

    ESP_LOGI(TAG, "AXP192 初始化完成");
    return ESP_OK;
}

esp_err_t bsp_power_set_speaker(bool en)   // IO2 = reg 0x94 bit2,高有效
{
    return reg_update(0x94, en ? 0xFF : (uint8_t)~0x04, en ? 0x04 : 0x00);
}

esp_err_t bsp_power_set_led(bool on)       // IO1 = reg 0x94 bit1,低有效
{
    return reg_update(0x94, on ? 0xFD : 0xFF, on ? 0x00 : 0x02);
}

esp_err_t bsp_power_set_backlight(bool on) // DCDC3 = reg 0x12 bit1
{
    return reg_update(0x12, on ? 0xFF : (uint8_t)~0x02, on ? 0x02 : 0x00);
}

esp_err_t bsp_power_set_vibration(bool on) // LDO3 = reg 0x12 bit3
{
    return reg_update(0x12, on ? 0xFF : (uint8_t)~0x08, on ? 0x08 : 0x00);
}

float bsp_power_batt_voltage(void)
{
    uint16_t raw;
    if (reg_read12(0x78, &raw) != ESP_OK) return 0.0f;
    return raw * 1.1f / 1000.0f;   // 1.1 mV/LSB
}
