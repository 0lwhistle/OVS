idf_component_register(
    SRCS "wifi.c"
    INCLUDE_DIRS "."
    REQUIRES mem_pool esp_wifi esp_event esp_netif esp_timer freertos logger
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_DRV)
