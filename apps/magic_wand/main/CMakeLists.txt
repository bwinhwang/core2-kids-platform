idf_component_register(
    SRCS
        "app_main.c"
        "magic_wand.c"
        "spellbook.c"
        "wizard.c"
        "wand_fx.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        m5stack_core_2      # bsp_display_lock/unlock + LVGL
        core2_board         # platform bring-up + PORT.A external I2C
        core2_sleep         # two-stage power orchestration
        imu_mpu6886         # body motion (feeds core2_sleep)
        unit_gesture        # PAJ7620U2 gesture unit (PORT.A @0x73)
        unit_rgb            # 3x SK6812 wand RGB (PORT.B @G26)
        ledstrip_fx         # base LED strip
        esp_hw_support      # esp_random()(星雨咒/你好咒粒子抖动)
        haptics             # vibration motor
        audio_fx            # sound effects engine
        app_slot            # multi-app partition bootstrap
)
