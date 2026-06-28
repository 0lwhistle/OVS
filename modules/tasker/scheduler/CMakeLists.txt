idf_component_register(
    SRCS "scheduler.c"
    INCLUDE_DIRS "."
    REQUIRES 
	freertos
	task_manager
	task_worker
)