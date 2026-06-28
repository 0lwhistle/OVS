idf_component_register(
    SRCS "manager_queue.c"
    INCLUDE_DIRS "."
    REQUIRES 
	freertos 
	esp_log
	task_manager 
	task_worker
)