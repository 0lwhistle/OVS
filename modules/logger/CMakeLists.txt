idf_component_register(
    SRCS "logger.c"
    INCLUDE_DIRS "."
    REQUIRES 
	freertos
	task_manager
	task_worker
)