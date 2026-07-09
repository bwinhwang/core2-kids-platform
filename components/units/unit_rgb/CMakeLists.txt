idf_component_register(
    SRCS "unit_rgb.c"
    INCLUDE_DIRS "include"
    REQUIRES led_strip   # espressif/led_strip（RMT 后端）
)
