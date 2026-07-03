idf_component_register(
    SRCS
        "app_main.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        m5stack_core_2      # bsp_display_lock/unlock + LVGL
        core2_board         # 平台一键 bring-up
        core2_sleep         # 两级省电(选择页久置也要打盹)
        imu_mpu6886         # 读加速度喂 core2_sleep
        audio_fx            # 点击/问候音
        haptics             # 点击/问候震动
        app_slot            # 游戏槽探测 + 启动
)
