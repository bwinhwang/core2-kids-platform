idf_component_register(
    SRCS "imu_mpu6886.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c   # driver/i2c_master.h（句柄类型出现在公共头里）
)
