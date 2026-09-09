idf_component_register(
    SRCS "holder.c"
    INCLUDE_DIRS "."
    REQUIRES mem_pool logger freertos esp_timer
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_CORE)
