// bsp_leds.c —— SK6812 ×10 灯条能力(Bottom2, G25)
//
// 只提供“点灯”能力(裸 0~255,不做亮度封顶);雨点/呼吸等动画属应用逻辑。
// 亮度请由应用经 kids_safety.h 的 KIDS_MAX_BRIGHTNESS 限幅后再传入。
#include "bsp.h"
#include "bsp_priv.h"
#include "led_strip.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "bsp_leds";
static led_strip_handle_t s_strip;
static const int s_layout[BSP_LED_COUNT] = BSP_LED_LAYOUT;   // 逻辑→物理

esp_err_t bsp_leds_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = BSP_PIN_LED_DATA,
        .max_leds       = BSP_LED_COUNT,
        .led_model      = LED_MODEL_SK6812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = { .invert_out = false },
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src       = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = { .with_dma = false },
    };
    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip), TAG, "new strip");
    ESP_RETURN_ON_ERROR(led_strip_clear(s_strip), TAG, "clear");
    ESP_LOGI(TAG, "灯条就绪(%d×SK6812 @ G%d)", BSP_LED_COUNT, BSP_PIN_LED_DATA);
    return ESP_OK;
}

esp_err_t bsp_leds_set(int logical_idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (logical_idx < 0 || logical_idx >= BSP_LED_COUNT) return ESP_ERR_INVALID_ARG;
    return led_strip_set_pixel(s_strip, s_layout[logical_idx], r, g, b);
}

esp_err_t bsp_leds_clear(void)
{
    return led_strip_clear(s_strip);
}

esp_err_t bsp_leds_refresh(void)
{
    return led_strip_refresh(s_strip);
}
