set(COMPONENT_NAME st7789)
idf_component_register(
    SRCS "st7789.c"
    INCLUDE_DIRS "."
    REQUIRES mem_pool spi_drv event_bus logger dtbs esp_driver_gpio
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_DRV)
