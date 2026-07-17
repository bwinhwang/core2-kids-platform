#include "power_monitor.h"

#include "esp_log.h"

#include "core2_power.h"

static const char *TAG = "power_monitor";

// 12-bit ADC:高字节 + 低字节低 4 位(核实来源见头文件注释)
static inline uint16_t combine12(uint8_t hi, uint8_t lo) { return ((uint16_t)hi << 4) | (lo & 0x0F); }
// 13-bit ADC(电池充放电电流专属):高字节 + 低字节低 5 位
static inline uint16_t combine13(uint8_t hi, uint8_t lo) { return ((uint16_t)hi << 5) | (lo & 0x1F); }
// 32-bit 库仑计累加器:按大端序假设(未逐字节核实到 Read32bit 函数体,见头文件注释)
static inline uint32_t combine32(const uint8_t b[4])
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

esp_err_t power_monitor_init(void)
{
    // SetAdcState(true) 等价写法:reg 0x82 写 0xFF 开全部 ADC 通道(含 VBUS 电压/电流)。
    // Confirmed via m5stack/M5Core2 AXP192.cpp(见头文件注释)。整字节写不做 RMW——
    // 该寄存器目前全仓只有本组件会碰,不与 core2_power/core2_board 已用的 REG 0x12
    // 等寄存器冲突。
    esp_err_t err = core2_power_write_reg(0x82, 0xFF);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "开 ADC 通道失败(reg 0x82): %s", esp_err_to_name(err));
        return err;
    }
    // EnableCoulombcounter():reg 0xB8 写 0x80。
    err = core2_power_write_reg(0xB8, 0x80);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "使能库仑计失败(reg 0xB8): %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "power_monitor 就绪(ADC 全开 + 库仑计已使能)");
    return ESP_OK;
}

esp_err_t power_monitor_read(power_telemetry_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    uint8_t buf2[2];
    uint8_t buf4[4];
    esp_err_t err;

    // 电池电压:reg 0x78(12-bit),ADCLSB=1.1mV
    err = core2_power_read_regs(0x78, buf2, 2);
    if (err != ESP_OK) { ESP_LOGW(TAG, "读电池电压失败: %s", esp_err_to_name(err)); return err; }
    out->batt_mv = (int)(combine12(buf2[0], buf2[1]) * 1.1f + 0.5f);

    // 电池充电电流:reg 0x7A(13-bit),ADCLSB=0.5mA
    err = core2_power_read_regs(0x7A, buf2, 2);
    if (err != ESP_OK) { ESP_LOGW(TAG, "读电池充电电流失败: %s", esp_err_to_name(err)); return err; }
    out->batt_charge_ma = (int)(combine13(buf2[0], buf2[1]) * 0.5f + 0.5f);

    // 电池放电电流:reg 0x7C(13-bit),ADCLSB=0.5mA
    err = core2_power_read_regs(0x7C, buf2, 2);
    if (err != ESP_OK) { ESP_LOGW(TAG, "读电池放电电流失败: %s", esp_err_to_name(err)); return err; }
    out->batt_discharge_ma = (int)(combine13(buf2[0], buf2[1]) * 0.5f + 0.5f);

    // VBUS 电压:reg 0x5A(12-bit),ADCLSB=1.7mV
    err = core2_power_read_regs(0x5A, buf2, 2);
    if (err != ESP_OK) { ESP_LOGW(TAG, "读 VBUS 电压失败: %s", esp_err_to_name(err)); return err; }
    out->vbus_mv = (int)(combine12(buf2[0], buf2[1]) * 1.7f + 0.5f);

    // VBUS 电流:reg 0x5C(12-bit),ADCLSB=0.375mA
    err = core2_power_read_regs(0x5C, buf2, 2);
    if (err != ESP_OK) { ESP_LOGW(TAG, "读 VBUS 电流失败: %s", esp_err_to_name(err)); return err; }
    out->vbus_ma = (int)(combine12(buf2[0], buf2[1]) * 0.375f + 0.5f);

    // 电源状态:reg 0x00(8-bit),bit5=VBUS 在位,bit2=正在充电
    uint8_t status;
    err = core2_power_read_regs(0x00, &status, 1);
    if (err != ESP_OK) { ESP_LOGW(TAG, "读电源状态失败: %s", esp_err_to_name(err)); return err; }
    out->vbus_present = (status & 0x20) != 0;
    out->charging     = (status & 0x04) != 0;

    // 库仑计:充电累加 reg 0xB0(32-bit)/ 放电累加 reg 0xB4(32-bit)
    err = core2_power_read_regs(0xB0, buf4, 4);
    if (err != ESP_OK) { ESP_LOGW(TAG, "读库仑计充电累加失败: %s", esp_err_to_name(err)); return err; }
    uint32_t coin = combine32(buf4);
    err = core2_power_read_regs(0xB4, buf4, 4);
    if (err != ESP_OK) { ESP_LOGW(TAG, "读库仑计放电累加失败: %s", esp_err_to_name(err)); return err; }
    uint32_t coout = combine32(buf4);
    // Confirmed via m5stack/M5Core2 GetCoulombData(): 65536*0.5*(coin-coout)/3600/25.0
    out->coulomb_mah = 65536.0f * 0.5f * (float)(int32_t)(coin - coout) / 3600.0f / 25.0f;

    return ESP_OK;
}

esp_err_t power_monitor_coulomb_reset(void)
{
    // ClearCoulombcounter():reg 0xB8 写 0xA0
    esp_err_t err = core2_power_write_reg(0xB8, 0xA0);
    if (err != ESP_OK) ESP_LOGW(TAG, "库仑计清零失败: %s", esp_err_to_name(err));
    return err;
}
