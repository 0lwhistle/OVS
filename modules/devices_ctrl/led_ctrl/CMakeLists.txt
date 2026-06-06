idf_component_register(
    SRCS "led_ctrl.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_common gpio_ctrl
)
