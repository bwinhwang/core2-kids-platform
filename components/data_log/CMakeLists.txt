idf_component_register(
    SRCS "data_log.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer
    PRIV_REQUIRES spiffs   # v2 SPIFFS 离线录制(esp_vfs_spiffs.h 只在 .c 里用,不出现在公共头)
)
