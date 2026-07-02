idf_component_register(
    SRCS "core2_board.c"
    INCLUDE_DIRS "include"
    REQUIRES
        m5stack_core_2    # BSP:I2C/屏/LVGL/喇叭供电(公共头暴露 lvgl 类型)
        esp_driver_i2c    # i2c_master 句柄类型 + probe
        core2_power       # AXP192 直控(EXTEN/DCDC3)
        ledstrip_fx       # 底座灯带
        audio_fx          # 音效引擎
        haptics           # 震动
        imu_mpu6886       # IMU
)
