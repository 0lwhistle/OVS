idf_component_register(
    SRCS "tasker.c"
    INCLUDE_DIRS "."
    REQUIRES 
	freertos
	tasker_worker
)
