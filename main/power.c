#include "power.h"

#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "power";

#define AXP192_ADDR 0x34

static i2c_master_dev_handle_t s_axp;

// 读—改—写一个 AXP192 寄存器:清 clr 位、置 set 位。
static esp_err_t axp_rmw(uint8_t reg, uint8_t clr, uint8_t set)
{
    uint8_t val = 0;
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_axp, &reg, 1, &val, 1, 1000),
                        TAG, "AXP 读 0x%02X 失败", reg);
    uint8_t out[2] = { reg, (uint8_t)((val & ~clr) | set) };
    return i2c_master_transmit(s_axp, out, sizeof(out), 1000);
}

void power_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = AXP192_ADDR,
        .scl_speed_hz    = 400000,
    };
    if (i2c_master_bus_add_device(bus, &cfg, &s_axp) != ESP_OK) {
        ESP_LOGE(TAG, "AXP192 设备添加失败,无法开 5V 总线(灯带将不亮)");
        s_axp = NULL;
        return;
    }
    // 复刻 M5Core2 SetBusPowerMode(0):GPIO0 设 LDOio 3.3V 输出 + 使能 EXTEN → 开 SY7088 升压。
    esp_err_t err = ESP_OK;
    if (err == ESP_OK) err = axp_rmw(0x91, 0xF0, 0xF0);  // GPIO0 LDOio0 输出电压 3.3V(高4位=0xF)
    if (err == ESP_OK) err = axp_rmw(0x90, 0x07, 0x02);  // GPIO0 功能=LDO 输出模式
    if (err == ESP_OK) err = axp_rmw(0x12, 0x00, 0x40);  // REG12 bit6 EXTEN=1
    if (err != ESP_OK) ESP_LOGE(TAG, "开 5V 总线写 AXP192 出错: %s", esp_err_to_name(err));
    else               ESP_LOGI(TAG, "已开启 M-Bus 5V(AXP192 EXTEN)→ 底座灯带供电");
}

void power_bus_5v(bool on)
{
    if (!s_axp) return;
    esp_err_t err = axp_rmw(0x12, on ? 0x00 : 0x40, on ? 0x40 : 0x00);  // EXTEN 置/清
    if (err != ESP_OK) ESP_LOGW(TAG, "切换 M-Bus 5V(%d)失败: %s", on, esp_err_to_name(err));
}

void power_backlight(bool on)
{
    if (!s_axp) return;
    // Core2 背光 = AXP192 DCDC3,使能位在 REG 0x12 bit1(0x02)。BSP 的 brightness 0%
    // 只降 DCDC3 电压(REG 0x27)不断电,仍亮;要真正熄屏必须清 bit1 关掉 DCDC3。
    esp_err_t err = axp_rmw(0x12, on ? 0x00 : 0x02, on ? 0x02 : 0x00);  // DCDC3 置/清
    if (err != ESP_OK) ESP_LOGW(TAG, "切换背光 DCDC3(%d)失败: %s", on, esp_err_to_name(err));
}
