set(COMPONENT_NAME uart_drv)
idf_component_register(
    SRCS "uart_drv.c"
    INCLUDE_DIRS "."
    REQUIRES dtbs logger esp_driver_uart
)
