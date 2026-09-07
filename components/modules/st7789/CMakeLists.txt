set(COMPONENT_NAME st7789)
idf_component_register(
    SRCS "st7789.c"
    INCLUDE_DIRS "."
    REQUIRES spi_drv event_bus logger dtbs esp_driver_gpio
)
