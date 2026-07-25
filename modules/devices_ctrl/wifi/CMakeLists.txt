idf_component_register(
    SRCS "wifi.c"
    INCLUDE_DIRS "."
    # 声明这个组件依赖哪些 IDF 组件
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_hw_support esp_timer
)
