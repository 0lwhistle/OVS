idf_component_register(
    SRCS "task_worker.c"
    INCLUDE_DIRS "."
    REQUIRES 
	freertos
	task_manager
)