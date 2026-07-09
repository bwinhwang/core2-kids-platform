idf_component_register(
    SRCS "app_slot.c"
    INCLUDE_DIRS "include"
    REQUIRES
        app_update          # esp_ota_set_boot_partition / esp_ota_get_partition_description
        esp_partition
        esp_app_format      # esp_app_desc_t
    PRIV_REQUIRES
        esp_system          # esp_restart
)
