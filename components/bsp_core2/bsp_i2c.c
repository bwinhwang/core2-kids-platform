// bsp_i2c.c —— 两条 I2C 总线的管理 + bsp_init() 平台初始化编排
//
// 内部总线(G21/G22):板载 AXP192/MPU6886/触摸/RTC。PORT.A(G32/G33):外接 UNIT,
// 惰性创建(没接 I2C UNIT 就不占用 I2C1)。初始化顺序严格按 Core2 §2(AXP192 最先)。
#include "bsp.h"
#include "bsp_priv.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "bsp";
static i2c_master_bus_handle_t s_internal;
static i2c_master_bus_handle_t s_port_a;

static esp_err_t make_bus(i2c_port_t port, int sda, int scl, i2c_master_bus_handle_t *out)
{
    i2c_master_bus_config_t cfg = {
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .i2c_port          = port,
        .sda_io_num        = sda,
        .scl_io_num        = scl,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, out);
}

i2c_master_bus_handle_t bsp_i2c_internal(void)
{
    return s_internal;
}

i2c_master_bus_handle_t bsp_i2c_port_a(void)
{
    if (!s_port_a) {
        if (make_bus(BSP_PORT_A_I2C_PORT, BSP_PIN_PORTA_SDA, BSP_PIN_PORTA_SCL,
                     &s_port_a) != ESP_OK) {
            ESP_LOGE(TAG, "PORT.A I2C 创建失败");
            return NULL;
        }
        ESP_LOGI(TAG, "PORT.A 外部 I2C 就绪(SDA=%d SCL=%d)",
                 BSP_PIN_PORTA_SDA, BSP_PIN_PORTA_SCL);
    }
    return s_port_a;
}

esp_err_t bsp_init(void)
{
    ESP_LOGI(TAG, "平台初始化:Core2 v1.0 + M5GO Bottom2");

    // 1) 内部 I2C 总线(AXP192 与 MPU6886 等共用)
    ESP_RETURN_ON_ERROR(make_bus(BSP_I2C_INTERNAL_PORT, BSP_PIN_I2C_INT_SDA,
                                 BSP_PIN_I2C_INT_SCL, &s_internal), TAG, "internal i2c");

    // 2) AXP192 必须最先(否则屏黑/没声/没触摸);返回时 SPK_EN 仍关
    ESP_RETURN_ON_ERROR(bsp_power_init(s_internal), TAG, "power");

    // 3) IMU(WHO_AM_I 确认为 MPU6886)
    ESP_RETURN_ON_ERROR(bsp_imu_init(s_internal), TAG, "imu");

#if CONFIG_BSP_ENABLE_LEDS
    ESP_RETURN_ON_ERROR(bsp_leds_init(), TAG, "leds");
#endif

#if CONFIG_BSP_ENABLE_DISPLAY
    ESP_RETURN_ON_ERROR(bsp_power_set_backlight(true), TAG, "backlight");
    ESP_RETURN_ON_ERROR(bsp_display_init(), TAG, "display");
#if CONFIG_BSP_ENABLE_TOUCH
    ESP_RETURN_ON_ERROR(bsp_touch_init(s_internal), TAG, "touch");
#endif
#endif

#if CONFIG_BSP_ENABLE_AUDIO
    // I2S 先喂静音帧,再拉高 SPK_EN(防上电爆音,见 Core2 §3)
    ESP_RETURN_ON_ERROR(bsp_audio_init(), TAG, "audio");
    ESP_RETURN_ON_ERROR(bsp_power_set_speaker(true), TAG, "spk on");
    vTaskDelay(pdMS_TO_TICKS(30));
#endif

    ESP_LOGI(TAG, "平台就绪");
    return ESP_OK;
}
