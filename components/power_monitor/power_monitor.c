#include "power_monitor.h"

#include <string.h>

#include "esp_log.h"
#include "core2_power.h"

static const char *TAG = "power_monitor";

#define REG_STATUS   0x00   // bit5=VBUS 在位, bit2=充电中
#define REG_VBUS_V   0x5A   // 12bit ×1.7mV
#define REG_BAT_V    0x78   // 12bit ×1.1mV;紧接 0x7A 充电流 / 0x7C 放电流(13bit ×0.5mA)
#define REG_ADC_EN1  0x82

static bool s_ready;

// AXP192 ADC 数据都是"高字节整字节 + 低字节右对齐若干位"(见头文件出处)
static inline int u12(const uint8_t *p) { return ((int)p[0] << 4) | (p[1] & 0x0F); }
static inline int u13(const uint8_t *p) { return ((int)p[0] << 5) | (p[1] & 0x1F); }

esp_err_t power_monitor_init(void)
{
    // 0x82 整个寄存器就是 8 路 ADC 使能位,没有别的功能挤在里面,可整字节写
    // (与 M5Core2 AXP192::Begin 的 Write1Byte(0x82, 0xff) 一致)。
    esp_err_t err = core2_power_write_reg(REG_ADC_EN1, 0xFF);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "使能 AXP192 ADC 失败(%s):电量读数不可用", esp_err_to_name(err));
        return err;
    }
    s_ready = true;
    power_status_t st;
    if (power_monitor_read(&st) == ESP_OK) {
        ESP_LOGI(TAG, "电量:%dmV(~%d%%)%+dmA%s", st.bat_mv, st.pct, st.bat_ma,
                 st.usb ? " [USB 在位]" : "");
    }
    return ESP_OK;
}

int power_monitor_pct(int bat_mv)
{
    // 单节锂电轻载放电曲线的分段近似(mV → %)。曲线中段平坦,别指望线性外插。
    static const int mv[]  = { 3300, 3400, 3500, 3600, 3680, 3750, 3850, 3950, 4050, 4150 };
    static const int pct[] = {    0,    5,   10,   20,   30,   45,   60,   75,   90,  100 };
    const int n = sizeof(mv) / sizeof(mv[0]);
    if (bat_mv <= mv[0])     return 0;
    if (bat_mv >= mv[n - 1]) return 100;
    for (int i = 1; i < n; i++) {
        if (bat_mv < mv[i]) {
            return pct[i - 1] + (bat_mv - mv[i - 1]) * (pct[i] - pct[i - 1]) / (mv[i] - mv[i - 1]);
        }
    }
    return 100;
}

esp_err_t power_monitor_read(power_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    uint8_t st = 0, bat[6] = { 0 }, vbus[2] = { 0 };
    esp_err_t err = core2_power_read_regs(REG_STATUS, &st, 1);
    // 0x78~0x7D 连续:电压 / 充电流 / 放电流 一笔读完
    if (err == ESP_OK) err = core2_power_read_regs(REG_BAT_V, bat, sizeof(bat));
    if (err == ESP_OK) err = core2_power_read_regs(REG_VBUS_V, vbus, sizeof(vbus));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "读 AXP192 电量失败: %s", esp_err_to_name(err));
        memset(out, 0, sizeof(*out));
        return err;
    }

    out->bat_mv   = u12(&bat[0]) * 11 / 10;                  // ×1.1mV
    out->bat_ma   = (u13(&bat[2]) - u13(&bat[4])) / 2;       // ×0.5mA,充电为正
    out->vbus_mv  = u12(vbus) * 17 / 10;                     // ×1.7mV
    out->usb      = (st & 0x20) != 0;
    out->charging = (st & 0x04) != 0;
    out->pct      = power_monitor_pct(out->bat_mv);
    return ESP_OK;
}
