idf_component_register(
    SRCS
        "app_main.c"
        "unit_bench_scan.c"
        "unit_bench_ui.c"
    INCLUDE_DIRS "."
    REQUIRES
        m5stack_core_2          # bsp_display_lock/unlock + LVGL 类型
        core2_board              # 平台一键 bring-up(顺带开 PORT.A/PORT.C 5V/EXTEN)
        core2_sleep              # 两级省电编排(打盹;DEEP 事实上禁用,见 README)
        imu_mpu6886              # 机身动作量(喂 core2_sleep)
        app_slot                 # 多 App 分区自举(回 launcher)
        audio_fx
        haptics
        kv_store                 # 超声波零点标定持久化
        data_log                 # 串口 CSV 导出
        power_monitor            # 状态栏电量/USB
        ui_kit                   # 状态栏/数值卡/chart/列表菜单
        unit_probe               # PORT.A 全总线扫描
        unit_dlight
        unit_ultrasonic
        unit_gesture
        unit_8encoder
        chain_bus                # Chain UART 传输层(PORT.C/UART2)
        unit_chain_encoder
        unit_chain_joystick
)
