idf_component_register(
    SRCS
        "app_main.c"
        "power_lab_ctl.c"
        "power_lab_ui.c"
    INCLUDE_DIRS "."
    REQUIRES
        m5stack_core_2    # bsp_display_lock/unlock + bsp_display_brightness_set + LVGL 类型
        core2_board       # 平台一键 bring-up
        core2_power       # core2_power_bus_5v/backlight(负载矩阵的 EXTEN/背光两项直控)
        core2_sleep       # core2_sleep_force_stage(休眠演练)
        app_slot          # 多 App 分区自举(回 launcher)
        audio_fx          # 开机问候 + 喇叭测试音负载项
        haptics           # 开机问候 + 震动测试负载项
        ledstrip_fx       # 灯带亮度负载项
        power_monitor     # AXP192 遥测(电压/电流/充电状态)
        data_log          # SPIFFS 离线录制(v2 API,见 components/data_log/README.md)
        ui_kit            # 状态栏/数值卡/chart/列表菜单
        esp_pm            # CPU 锁频矩阵项(esp_pm_configure,CONFIG_PM_ENABLE 见 sdkconfig.defaults)
)
