set(COMPONENT_NAME cst816s)
idf_component_register(
    SRCS "cst816s.c"
    INCLUDE_DIRS "."
    REQUIRES i2c_drv event_bus tasker_api logger dtbs esp_driver_gpio
)
