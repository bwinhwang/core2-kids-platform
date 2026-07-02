#include "core2_board.h"

#include "esp_log.h"
#include "esp_check.h"
#include "bsp/m5stack_core_2.h"

#include "core2_power.h"
#include "ledstrip_fx.h"
#include "audio_fx.h"
#include "haptics.h"
#include "imu_mpu6886.h"

static const char *TAG = "core2_board";

static lv_display_t *s_disp;
static i2c_master_bus_handle_t s_i2c;

bool core2_board_i2c_scan(void)
{
    ESP_LOGI(TAG, "扫描内部 I2C 总线 (G21/G22)…");
    bool found_imu = false;
    for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(s_i2c, addr, 50) == ESP_OK) {
            const char *name = "?";
            switch (addr) {
                case 0x34: name = "AXP192 (电源)";   break;
                case 0x38: name = "FT6336U (触摸)";  break;
                case 0x51: name = "BM8563 (RTC)";    break;
                case 0x68: name = "MPU6886 (IMU)"; found_imu = true; break;
            }
            ESP_LOGI(TAG, "  发现 0x%02X  %s", addr, name);
        }
    }
    return found_imu;
}

esp_err_t core2_board_init(const core2_board_cfg_t *cfg)
{
    core2_board_cfg_t c = cfg ? *cfg : CORE2_BOARD_CFG_KIDS_DEFAULT;

    // 1) 内部 I2C(AXP192/触摸/RTC/IMU 都在这条总线)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "bsp_i2c_init 失败");
    s_i2c = bsp_i2c_get_handle();

    // 2) 屏 + LVGL(BSP 同时做 AXP192 屏电/背光基础配置)
    s_disp = bsp_display_start();
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG,
                        "bsp_display_start 失败(检查 CONFIG_BSP_PMU_AXP192)");
    ESP_RETURN_ON_ERROR(bsp_display_brightness_set(c.brightness_pct), TAG, "设背光失败");

    // 3) I2C 自检(顺带确认底座 IMU 在位)
    bool imu_present = core2_board_i2c_scan();

    // 4) AXP192 直控绑定(EXTEN/DCDC3)。必须在 bsp_display_start 之后:
    //    BSP 初始化会重写 REG 0x12,先绑先开会被清掉。
    ESP_RETURN_ON_ERROR(core2_power_init(s_i2c), TAG, "core2_power_init 失败");

    // 5) 灯带:先供电(M-Bus 5V/EXTEN)再起驱动,顺序反了就是"数据在跑灯全黑"
    if (c.enable_leds) {
        ESP_RETURN_ON_ERROR(core2_power_bus_5v(true), TAG, "开 M-Bus 5V 失败");
        ESP_RETURN_ON_ERROR(ledstrip_fx_init(), TAG, "灯带初始化失败");
        ledstrip_fx_set_max_brightness(c.led_max_brightness);
    }

    // 6) 喇叭(整局保持 open,防爆音)
    if (c.enable_audio) {
        ESP_RETURN_ON_ERROR(audio_fx_init(), TAG, "音频初始化失败");
    }

    // 7) 震动(LDO3 供电由 BSP feature 管)
    if (c.enable_haptics) {
        ESP_RETURN_ON_ERROR(haptics_init(), TAG, "震动初始化失败");
    }

    // 8) IMU(MPU6886 @0x68,复用内部 I2C 总线,勿自建总线)
    if (c.enable_imu) {
        if (!imu_present) {
            ESP_LOGE(TAG, "✗ 没扫到 0x68:Bottom2 底座没接?(IMU 和电池都来自底座)");
            return ESP_ERR_NOT_FOUND;
        }
        ESP_RETURN_ON_ERROR(imu_mpu6886_init(s_i2c), TAG,
                            "IMU 初始化失败(WHO_AM_I 应为 0x19)");
    }

    ESP_LOGI(TAG, "平台就绪:屏%s 灯带%s 音频%s 震动%s IMU%s",
             "✓", c.enable_leds ? "✓" : "-", c.enable_audio ? "✓" : "-",
             c.enable_haptics ? "✓" : "-", c.enable_imu ? "✓" : "-");
    return ESP_OK;
}

lv_display_t *core2_board_display(void) { return s_disp; }

i2c_master_bus_handle_t core2_board_i2c(void) { return s_i2c; }
