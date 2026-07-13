idf_component_register(
    SRCS "screenshot.c"
    INCLUDE_DIRS "include"
    REQUIRES
        m5stack_core_2      # BSP:bsp_display_lock + LVGL 类型(公共头暴露)
    PRIV_REQUIRES
        esp_driver_uart     # UART0 收触发命令
        esp_rom             # esp_rom_crc32_le
)
