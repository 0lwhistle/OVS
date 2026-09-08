idf_component_register(
    SRCS "ota.c"
    INCLUDE_DIRS "."
    REQUIRES app_update sha256 dtbs esp_partition esp_app_format esp_system esp_timer freertos logger
)
