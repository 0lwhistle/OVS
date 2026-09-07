set(COMPONENT_NAME i2c_drv)
idf_component_register(
    SRCS "i2c_drv.c"
    INCLUDE_DIRS "."
    REQUIRES dtbs logger esp_driver_i2c
)
