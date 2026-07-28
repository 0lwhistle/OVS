idf_component_register(
    SRCS "task_worker.c" "tasker_test.c"
    INCLUDE_DIRS "."
    REQUIRES 
	freertos
	task_manager
	tasker
	logger
)
