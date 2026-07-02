idf_component_register(
    SRCS "core2_sleep.c"
    INCLUDE_DIRS "include"
    REQUIRES
        motion_detect     # 判据:机身动作量 + 唤醒去抖(公共头暴露其类型)
    PRIV_REQUIRES
        m5stack_core_2    # bsp_display_brightness_set
        core2_power       # 断/恢复 DCDC3、M-Bus 5V
        ledstrip_fx       # 灯带基础模式联动
        haptics           # 唤醒轻震
)
