set(COMPONENT_NAME spi_drv)
idf_component_register(
    SRCS "spi_drv.c"
    INCLUDE_DIRS "."
    REQUIRES dtbs logger esp_driver_spi esp_driver_gpio
)
