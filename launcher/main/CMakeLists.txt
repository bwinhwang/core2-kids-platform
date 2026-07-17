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
        app_slot            # 评估槽探测 + 启动 + app_slot_info 数据驱动
        power_monitor       # 状态栏电量/USB(2026-07-17 平台转向新增)
        ui_kit              # 状态栏/卡片渲染控件(2026-07-17 平台转向新增)
)
