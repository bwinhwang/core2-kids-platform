idf_component_register(
    SRCS "power_monitor.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        core2_power   # 复用其已绑定的 AXP192 句柄读 ADC 寄存器
)
