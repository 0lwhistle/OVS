idf_component_register(
    SRCS "heartbeat.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_timer freertos tasker_api tasker_api logger
)
