idf_component_register(
    SRCS "web.c"
         "web_ota.c"
         "${CMAKE_SOURCE_DIR}/thirdparty/mongoose/mongoose.c"
         "web_data/web_data.c"
    INCLUDE_DIRS "."
                 "${CMAKE_SOURCE_DIR}/thirdparty/mongoose"
                 "web_data"
    REQUIRES mem_pool spiffs freertos ota net_mgr dtbs wifi heartbeat tasker logger esp_timer
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_WEB)
