idf_component_register(
    SRCS "ota.c" "ota_sha256.c"
    INCLUDE_DIRS "."
    REQUIRES app_update esp_partition esp_app_format esp_system esp_timer freertos logger
)
