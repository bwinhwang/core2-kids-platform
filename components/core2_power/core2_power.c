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

esp_err_t core2_power_shutdown(void)
{
    // AXP192 REG 0x32 bit7=1 → 立即断电(与 M5Core2 AXP192::PowerOff() 一致,
    // Confirmed via github.com/m5stack/M5Core2 src/AXP192.cpp)。0x32 还管电池检测/
    // CHGLED,必须 RMW 保住其余位,不能整字节写(勿改用 core2_power_write_reg)。
    ESP_LOGW(TAG, "软件关机:置 AXP192 0x32 bit7");
    return axp_rmw(0x32, 0x00, 0x80);
    // 正常不返回(芯片断电);若返回说明 I2C 写失败,交调用方处理。
}

esp_err_t core2_power_read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    if (!s_axp) return ESP_ERR_INVALID_STATE;
    if (!buf || len == 0) return ESP_ERR_INVALID_ARG;
    // 与 axp_rmw 内部读法一致:AXP192 是普通寄存器寻址芯片,组合读(repeated-start)安全,
    // 不属于 8Encoder 那类 MCU 从机"只在收到 STOP 才解析寄存器号"的坑(CLAUDE.md §10)。
    return i2c_master_transmit_receive(s_axp, &reg, 1, buf, len, 1000);
}

esp_err_t core2_power_write_reg(uint8_t reg, uint8_t val)
{
    if (!s_axp) return ESP_ERR_INVALID_STATE;
    uint8_t out[2] = { reg, val };
    return i2c_master_transmit(s_axp, out, sizeof(out), 1000);
}
