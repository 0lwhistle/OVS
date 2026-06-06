idf_component_register(
    SRCS "gpio_ctrl.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_common
)
