# 契约 I2 传感器快照缓存（[HUB]）
idf_component_register(
    SRCS "sensor_cache.c" "sensor_cache_port.c"
    INCLUDE_DIRS "."
    REQUIRES event_bus logger
    PRIV_REQUIRES freertos esp_timer
)
