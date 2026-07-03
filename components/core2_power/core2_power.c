#include "core2_power.h"

#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "core2_power";

#define AXP192_ADDR 0x34

static i2c_master_dev_handle_t s_axp;

// 读—改—写一个 AXP192 寄存器:清 clr 位、置 set 位。
// AXP192 各使能位挤在同一寄存器(REG 0x12 同时管 DCDC1/3、LDO2/3、EXTEN),
// 整字节写会误关别的 rail(屏电/喇叭电),必须 RMW。
static esp_err_t axp_rmw(uint8_t reg, uint8_t clr, uint8_t set)
{
    if (!s_axp) return ESP_ERR_INVALID_STATE;
    uint8_t val = 0;
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_axp, &reg, 1, &val, 1, 1000),
                        TAG, "AXP 读 0x%02X 失败", reg);
    uint8_t out[2] = { reg, (uint8_t)((val & ~clr) | set) };
    return i2c_master_transmit(s_axp, out, sizeof(out), 1000);
}

esp_err_t core2_power_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = AXP192_ADDR,
        .scl_speed_hz    = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_axp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AXP192 设备添加失败(%s):5V/背光控制不可用", esp_err_to_name(err));
        s_axp = NULL;
        return err;
    }
    // 复刻 M5Core2 SetBusPowerMode(0) 的一次性准备:GPIO0 配 LDOio 3.3V 输出。
    // (总线供电模式下 GPIO0 要输出 3.3V 给 M-Bus VDD;EXTEN 由 core2_power_bus_5v 单独开)
    if (err == ESP_OK) err = axp_rmw(0x91, 0xF0, 0xF0);  // GPIO0 LDOio0 输出电压 3.3V(高4位=0xF)
    if (err == ESP_OK) err = axp_rmw(0x90, 0x07, 0x02);  // GPIO0 功能=LDO 输出模式
    if (err != ESP_OK) ESP_LOGE(TAG, "AXP192 GPIO0/LDOio 配置失败: %s", esp_err_to_name(err));
    else               ESP_LOGI(TAG, "AXP192 已绑定(EXTEN/DCDC3 可控)");
    return err;
}

esp_err_t core2_power_bus_5v(bool on)
{
    esp_err_t err = axp_rmw(0x12, on ? 0x00 : 0x40, on ? 0x40 : 0x00);  // EXTEN 置/清
    if (err != ESP_OK) ESP_LOGW(TAG, "切换 M-Bus 5V(%d)失败: %s", on, esp_err_to_name(err));
    else if (on)       ESP_LOGI(TAG, "已开启 M-Bus 5V(EXTEN)→ 底座灯带供电");
    return err;
}

esp_err_t core2_power_backlight(bool on)
{
    esp_err_t err = axp_rmw(0x12, on ? 0x00 : 0x02, on ? 0x02 : 0x00);  // DCDC3 置/清
    if (err != ESP_OK) ESP_LOGW(TAG, "切换背光 DCDC3(%d)失败: %s", on, esp_err_to_name(err));
    return err;
}

bool core2_power_pek_pressed(void)
{
    if (!s_axp) return false;
    uint8_t reg = 0x46, val = 0;   // IRQ 状态3:bit1=PEK 短按,bit0=PEK 长按
    if (i2c_master_transmit_receive(s_axp, &reg, 1, &val, 1, 1000) != ESP_OK) return false;
    if (val & 0x03) {              // 写 1 清除
        uint8_t out[2] = { 0x46, (uint8_t)(val & 0x03) };
        i2c_master_transmit(s_axp, out, sizeof(out), 1000);
    }
    // 短按/长按都算"按过":BSP 配置(REG 0x36=0x4C)下按住 ≥1s 只报"长按",
    // 只认短按会漏掉按得稍久的一下(见头文件注释,实机踩坑)
    return (val & 0x03) != 0;
}
