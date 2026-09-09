set(COMPONENT_NAME i2s_drv)
idf_component_register(
    SRCS "i2s_drv.c"
    INCLUDE_DIRS "."
    REQUIRES mem_pool dtbs logger esp_driver_i2s
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_DRV)
