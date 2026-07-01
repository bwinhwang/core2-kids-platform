// bsp_priv.h —— BSP 内部编排用声明(应用不必调用)
#pragma once
#include "esp_err.h"
#include "driver/i2c_master.h"

esp_err_t bsp_power_init(i2c_master_bus_handle_t bus);   // AXP192 上电序列(SPK 先关)
esp_err_t bsp_imu_init(i2c_master_bus_handle_t bus);     // MPU6886
#if CONFIG_BSP_ENABLE_LEDS
esp_err_t bsp_leds_init(void);
#endif
#if CONFIG_BSP_ENABLE_AUDIO
esp_err_t bsp_audio_init(void);
#endif
#if CONFIG_BSP_ENABLE_DISPLAY
esp_err_t bsp_display_init(void);
#endif
#if CONFIG_BSP_ENABLE_TOUCH
esp_err_t bsp_touch_init(i2c_master_bus_handle_t bus);
#endif
