idf_component_register(
    SRCS "web.c" "mongoose/mongoose.c" "web_data/web_data.c"
    INCLUDE_DIRS "." "mongoose" "web_data"
    REQUIRES spiffs esp_http_server freertos ota heartbeat tasker wifi logger
)
