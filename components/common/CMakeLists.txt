idf_component_register(
    SRCS "app_nvs.c" "i2c_scan.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c nvs_flash
)
