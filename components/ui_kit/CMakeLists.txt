idf_component_register(
    SRCS
        "ui_status_bar.c"
        "ui_value_card.c"
        "ui_chart.c"
        "ui_list_menu.c"
    INCLUDE_DIRS "include"
    REQUIRES
        m5stack_core_2    # BSP:公共头暴露 lvgl.h(与 core2_board 同一复用套路)
)
