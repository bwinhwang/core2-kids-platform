idf_component_register(
    SRCS "touch_btns.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_i2c    # i2c_master 句柄类型(公共头暴露)
    PRIV_REQUIRES
        app_slot          # BtnA 回 launcher(return_to_factory)
        core2_power       # BtnC 关机(AXP192 0x32 bit7)
        haptics           # 三键反馈震动
        screenshot        # BtnB 截屏(screenshot_dump_now)
        esp_timer         # 长按计时 now_ms
        esp_system        # esp_restart
)
