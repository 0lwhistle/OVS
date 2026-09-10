set(COMPONENT_NAME audio_player)
idf_component_register(
    SRCS "audio_player.c"
         "audio_player_port_esp.c"
    INCLUDE_DIRS "."
    REQUIRES audio_module mem_pool event_bus tasker_api logger dtbs nvs_flash
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_AUDIO)
