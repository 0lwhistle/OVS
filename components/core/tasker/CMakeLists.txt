idf_component_register(
    SRCS "task_manager.c" "task_worker.c" "tasker_test.c"
    INCLUDE_DIRS "."
    REQUIRES mem_pool freertos esp_timer esp_driver_gptimer logger
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_CORE)
