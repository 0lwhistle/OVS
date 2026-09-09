set(COMPONENT_NAME spi_drv)
idf_component_register(
    SRCS "spi_drv.c"
    INCLUDE_DIRS "."
    REQUIRES mem_pool dtbs logger esp_driver_spi esp_driver_gpio
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_DRV)
