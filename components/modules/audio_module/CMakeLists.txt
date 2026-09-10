set(COMPONENT_NAME audio_module)
idf_component_register(
    SRCS "audio_module.c"
         "audio_port.c"
    INCLUDE_DIRS "."
    REQUIRES mem_pool i2s_drv event_bus tasker_api logger dtbs gpio
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_AUDIO)
