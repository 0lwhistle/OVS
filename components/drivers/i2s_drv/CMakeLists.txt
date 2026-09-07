set(COMPONENT_NAME i2s_drv)
idf_component_register(
    SRCS "i2s_drv.c"
    INCLUDE_DIRS "."
    REQUIRES dtbs logger esp_driver_i2s
)
