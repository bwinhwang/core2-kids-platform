idf_component_register(
    SRCS "chain_bus.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_uart   # driver/uart.h(uart_port_t 出现在公共头里)
    PRIV_REQUIRES esp_timer
)
