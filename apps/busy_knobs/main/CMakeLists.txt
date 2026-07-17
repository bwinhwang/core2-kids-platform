idf_component_register(
    SRCS
        "app_main.c"
        "knobs_game.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        m5stack_core_2      # bsp_display_lock/unlock + LVGL
        core2_board         # 平台一键 bring-up + PORT.A 外接 I2C
        core2_sleep         # 两级省电编排(打盹/深度省电/唤醒)
        imu_mpu6886         # 机身动作量(喂 core2_sleep)
        unit_8encoder       # 8 旋钮单元(PORT.A @0x41)
        ledstrip_fx         # 底座灯带
        haptics             # 震动马达
        audio_fx            # 音效引擎(五声音阶叮咚)
        app_slot            # 多 App 分区自举(回 launcher / 电源键退出)
)
