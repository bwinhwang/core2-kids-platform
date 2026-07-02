idf_component_register(
    SRCS "ledstrip_fx.c"
    INCLUDE_DIRS "include"
    REQUIRES led_strip   # espressif/led_strip（RMT 后端）
)
