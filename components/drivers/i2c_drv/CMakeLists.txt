set(COMPONENT_NAME i2c_drv)
idf_component_register(
    SRCS "i2c_drv.c"
    INCLUDE_DIRS "."
    REQUIRES mem_pool dtbs logger esp_driver_i2c
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_DRV)
