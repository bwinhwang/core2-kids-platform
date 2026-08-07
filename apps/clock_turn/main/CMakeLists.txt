idf_component_register(
    SRCS
        "main.c"
        "clock_model.c"
        "clock_ui.c"
        "feedback.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        m5stack_core_2         # bsp_display_lock/unlock + LVGL(clock_ui.c 画钟面/指针/信息区)
        core2_board             # 平台一键 bring-up(顺带开 PORT.C 5V/EXTEN,touch_btns 随之起)
        core2_sleep              # 两级省电编排(打盹/深度省电/唤醒)
        imu_mpu6886              # 机身动作量(喂 core2_sleep)
        chain_bus                # Chain UART 传输层(PORT.C/UART2)
        unit_chain_encoder       # Chain Encoder(U207)
        ledstrip_fx               # 底座灯带(反馈通道,§7)
        haptics                   # 震动马达(反馈通道,§7)
        audio_fx                  # 音效引擎(反馈通道,§7;2026-08-06 语音作废后唯一的声音来源)
        app_slot                 # 多 App 分区自举(回 launcher)
)
