idf_component_register(
    SRCS "unit_chain_encoder.c"
    INCLUDE_DIRS "include"
    REQUIRES chain_bus   # Chain UART 传输层
)
