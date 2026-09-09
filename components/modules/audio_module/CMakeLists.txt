set(COMPONENT_NAME audio_module)
idf_component_register(
    SRCS "audio_module.c"
    INCLUDE_DIRS "."
    REQUIRES mem_pool i2s_drv event_bus tasker logger dtbs
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_AUDIO)
