set(COMPONENT_NAME lora)
idf_component_register(
    SRCS "lora.c"
    INCLUDE_DIRS "."
    REQUIRES uart_drv event_bus tasker_api logger dtbs esp_driver_gpio
)
