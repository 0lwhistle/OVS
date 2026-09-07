set(COMPONENT_NAME w25q128)
idf_component_register(
    SRCS "w25q128.c"
    INCLUDE_DIRS "."
    REQUIRES spi_drv event_bus tasker logger dtbs
)
