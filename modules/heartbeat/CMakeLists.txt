idf_component_register(
    SRCS "heartbeat.c"
    INCLUDE_DIRS "."
    REQUIRES 
	app_update 
	esp_timer 
	wifi
	tasker
	PRIV_REQUIRES
)