# bsp_core2 —— 按 Kconfig 开关条件编译各能力
# 公共头 bsp.h 暴露 i2c 类型 → esp_driver_i2c 走 REQUIRES;
# 其余外设头只在各 .c 内部用 → 走 PRIV_REQUIRES(v6 的 driver 伞组件不再自动传递)。
# PRIV_REQUIRES 静态列全:裁剪某能力时对应 .c 不编译,多列的依赖无副作用。
set(srcs "bsp_power.c" "bsp_i2c.c" "bsp_imu.c")

if(CONFIG_BSP_ENABLE_LEDS)
    list(APPEND srcs "bsp_leds.c")
endif()
if(CONFIG_BSP_ENABLE_AUDIO)
    list(APPEND srcs "bsp_audio.c")
endif()
if(CONFIG_BSP_ENABLE_DISPLAY)
    list(APPEND srcs "bsp_display.c")
endif()
if(CONFIG_BSP_ENABLE_TOUCH)
    list(APPEND srcs "bsp_touch.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES esp_driver_i2s esp_driver_spi esp_lcd esp_lcd_ili9341 esp_lcd_touch_ft5x06 led_strip
)
