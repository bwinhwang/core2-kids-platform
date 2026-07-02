idf_component_register(
    SRCS
        "app_main.c"
        "physics.c"
        "render.c"
        "maze.c"
        "feedback.c"
        "game_state.c"
        "parent_menu.c"
        "power.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        m5stack_core_2      # bsp_* + LVGL
        esp_driver_i2c      # i2c_master_probe 总线扫描
        esp_hw_support      # esp_random()(过关彩纸)
        imu_mpu6886         # MPU6886 倾斜驱动
        ledstrip_fx         # 底座灯带
        haptics             # 震动马达
        maze_audio          # 音效
)
