idf_component_register(
    SRCS "net_mgr.c"
    INCLUDE_DIRS "."
    REQUIRES wifi nvs_flash dtbs espressif__mdns esp_wifi esp_event esp_netif esp_timer freertos event_bus logger
)
