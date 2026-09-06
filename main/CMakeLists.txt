idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES 
        tasker_api
        eventbus_api
        tasker
        event_bus
        logger
        wifi
        led
        beep
        gpio
        sr04
        ota
        web
        heartbeat
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        freertos
)
