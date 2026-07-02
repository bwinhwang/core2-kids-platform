idf_component_register(
    SRCS "haptics.c"
    INCLUDE_DIRS "include"
    REQUIRES m5stack_core_2   # bsp_feature_enable(BSP_FEATURE_VIBRATION)
)
