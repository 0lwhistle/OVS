set(COMPONENT_NAME uart_drv)
idf_component_register(
    SRCS "uart_drv.c"
    INCLUDE_DIRS "."
    REQUIRES mem_pool dtbs logger esp_driver_uart
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_DRV)
