idf_component_register(
    SRCS "shared_state.c" "app_state.c" "engine.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos
)
