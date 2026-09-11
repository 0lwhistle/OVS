# GT967 单层 5 点电容触控（GT9xx 系，真机适配 2026-09-10）
idf_component_register(
    SRCS "gt967.c"
    INCLUDE_DIRS "."
    REQUIRES i2c_drv event_bus tasker_api logger dtbs
    PRIV_REQUIRES freertos esp_driver_gpio
)
