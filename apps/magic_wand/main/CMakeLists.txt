idf_component_register(
    SRCS
        "app_main.c"
        "magic_wand.c"
        "garden.c"
        "firefly.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        m5stack_core_2      # bsp_display_lock/unlock + LVGL
        core2_board         # platform bring-up + PORT.A external I2C
        core2_sleep         # two-stage power orchestration
        imu_mpu6886         # body motion (feeds core2_sleep)
        unit_gesture        # PAJ7620U2 gesture unit (PORT.A @0x73), gesture mode + presence (v2.1)
        ledstrip_fx         # base LED strip
        haptics             # vibration motor
        audio_fx            # sound effects engine
        app_slot            # multi-app partition bootstrap
        # unit_rgb(魔法灯笼)是 P4,本阶段(P1)不接;SPEC.md §12 / §2
)
