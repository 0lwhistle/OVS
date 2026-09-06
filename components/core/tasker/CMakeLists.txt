idf_component_register(
    SRCS "task_manager.c" "task_worker.c" "tasker_test.c"
    INCLUDE_DIRS "."
    REQUIRES freertos esp_timer esp_driver_gptimer logger
)
