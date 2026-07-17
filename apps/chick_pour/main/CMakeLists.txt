idf_component_register(
    SRCS
        "app_main.c"
        "flock.c"
        "scene.c"
        "critters.c"
        "feedback.c"
        "game_state.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        m5stack_core_2      # bsp_* + LVGL(scene/critters/game_state 用)
        esp_hw_support      # esp_random()(初始散布抖动 + 每只增益抖动)
        core2_board         # 平台一键 bring-up
        core2_sleep         # 两级省电编排(打盹/深度省电/唤醒,纯 IMU 玩法全托管)
        imu_mpu6886         # MPU6886 倾斜驱动(与 tilt_maze 同一套标定)
        audio_fx            # 音效引擎(boing/叽嘎/五声音阶/派对)
        haptics             # 震动(弹出轻震/归家/派对)
        ledstrip_fx         # 底座灯带(归家扫圈/冲刺加亮/派对彩虹)
        app_slot            # 多 App 分区自举(回 launcher)
)
