idf_component_register(
    SRCS "tasker.c"
    INCLUDE_DIRS "."
    REQUIRES 
	freertos
	task_worker
	task_manager
	logger
)
