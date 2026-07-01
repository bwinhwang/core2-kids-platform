// bsp_touch.c —— FT6336U 电容触摸(内部 I2C 0x38)
//
// 用官方 esp_lcd_touch_ft5x06(FT5x06 系列驱动兼容 FT6336U,Core2 esp-bsp 亦用此),
// 不手搓寄存器。⚠️ 未上板验证:方向/坐标可能需上板调 swap_xy/mirror。
// Confirmed via esp-component-registry: esp_lcd_touch_ft5x06 v1.1.0 API。
#include "bsp.h"
#include "bsp_priv.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "bsp_touch";
static esp_lcd_touch_handle_t s_tp;

esp_err_t bsp_touch_init(i2c_master_bus_handle_t bus)
{
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io_cfg.scl_speed_hz = BSP_I2C_INTERNAL_HZ;   // 新 i2c_master 需要显式时钟
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io), TAG, "tp io");

    esp_lcd_touch_config_t cfg = {
        .x_max        = BSP_LCD_W,
        .y_max        = BSP_LCD_H,
        .rst_gpio_num = -1,                 // RST 走 AXP192 IO4(已复位)
        .int_gpio_num = -1,                 // 轮询(INT=G39 只读脚)
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(io, &cfg, &s_tp), TAG, "tp new");
    ESP_LOGW(TAG, "FT6336U 触摸就绪(⚠️ 未上板验证,方向需上板调)");
    return ESP_OK;
}

bool bsp_touch_read(uint16_t *x, uint16_t *y)
{
    esp_lcd_touch_read_data(s_tp);
    esp_lcd_touch_point_data_t pts[1];
    uint8_t cnt = 0;
    if (esp_lcd_touch_get_data(s_tp, pts, &cnt, 1) != ESP_OK || cnt == 0) return false;
    if (x) *x = pts[0].x;
    if (y) *y = pts[0].y;
    return true;
}
