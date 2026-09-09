# AHT30 温湿度传感器模块
# 
# 使用I2C总线通信，通过event_bus发布温湿度数据

idf_component_register(
    SRCS "aht30.c"
    INCLUDE_DIRS "."
    REQUIRES i2c_drv event_bus tasker_api logger dtbs
)
