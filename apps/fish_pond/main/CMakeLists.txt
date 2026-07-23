idf_component_register(
    SRCS
        "app_main.c"
        "chain_link.c"
        "pond.c"
        "boat.c"
        "fish.c"
        "bucket.c"
        "sprites.c"
        "feedback.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        m5stack_core_2         # bsp_display_lock/unlock + LVGL
        core2_board            # 平台一键 bring-up(顺带开 PORT.C 5V/EXTEN)
        core2_sleep            # 两级省电编排(打盹/深度省电/唤醒)
        imu_mpu6886            # 机身动作量(喂 core2_sleep)
        chain_bus              # Chain UART 传输层(PORT.C/UART2)
        unit_chain_encoder     # Chain Encoder(曲柄,U207)
        unit_chain_joystick    # Chain Joystick(船,U205)
        ledstrip_fx            # 底座灯带
        haptics                # 震动马达
        audio_fx               # 音效引擎
        app_slot               # 多 App 分区自举(回 launcher)
)
