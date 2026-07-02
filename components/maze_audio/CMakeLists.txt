idf_component_register(
    SRCS "maze_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES m5stack_core_2 esp_codec_dev   # BSP 喇叭 + esp_codec_dev
)
