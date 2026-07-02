idf_component_register(
    SRCS
        "app_main.c"
        "physics.c"
        "render.c"
        "maze.c"
        "feedback.c"
        "game_state.c"
        "parent_menu.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        m5stack_core_2      # bsp_* + LVGL(render/parent_menu/game_state 用)
        esp_hw_support      # esp_random()(过关彩纸)
        core2_board         # 平台一键 bring-up
        core2_sleep         # 两级省电编排(打盹/深度省电/唤醒)
        imu_mpu6886         # MPU6886 倾斜驱动
        ledstrip_fx         # 底座灯带
        haptics             # 震动马达
        audio_fx            # 音效引擎
)
