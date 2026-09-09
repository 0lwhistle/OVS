# main 组件垫片（ESP-IDF 要求工程必须包含名为 main 的组件，且组件名取自目录名，
# 因此保留本目录；实际应用代码全部位于 src/app/，请到那里修改）
idf_component_register(
    SRCS "${CMAKE_SOURCE_DIR}/src/app/main.c" "${CMAKE_SOURCE_DIR}/src/app/vfs_stress.c"
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/src/app"
    REQUIRES
        tasker_api
        eventbus_api
        tasker
        event_bus
        logger
        dtbs
        wifi
        led
        gpio
        i2s_drv
        i2c_drv
        spi_drv
        uart_drv
        ota
        net_mgr
        web
        heartbeat
        audio_module
        st7789
        aht30
        cst816s
        lora
        w25q128
        holder
        ovs_vfs
        internal_flash
        esp_littlefs
        esp_blockdev
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        freertos
        lvgl
)
