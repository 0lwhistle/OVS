idf_component_register(
    SRCS "web.c"
         "web_ota.c"
         "web_api_sysinfo.c"
         "${CMAKE_SOURCE_DIR}/thirdparty/mongoose/mongoose.c"
         "web_data/web_data.c"
    INCLUDE_DIRS "."
                 "${CMAKE_SOURCE_DIR}/thirdparty/mongoose"
                 "web_data"
    REQUIRES mem_pool spiffs freertos ota net_mgr dtbs wifi heartbeat tasker_api logger esp_timer holder event_bus sensor_cache sysinfo time_svc
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_WEB)
# [WEB] HUB 批次①（sensor_cache/sysinfo/time_svc）已交付：置 1 切到真实数据
# 接口（REQUIRES 已补 sensor_cache sysinfo time_svc）
target_compile_definitions(${COMPONENT_LIB} PRIVATE WEB_API_USE_HUB=1)
