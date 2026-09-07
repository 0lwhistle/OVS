set(COMPONENT_NAME audio_module)
idf_component_register(
    SRCS "audio_module.c"
    INCLUDE_DIRS "."
    REQUIRES i2s_drv event_bus tasker logger dtbs
)
