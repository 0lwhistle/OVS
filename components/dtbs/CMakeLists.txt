idf_component_register(
    SRCS "dtree.c"
         "dtb_ab.c"
         "${CMAKE_SOURCE_DIR}/thirdparty/cJSON/cJSON.c"
    INCLUDE_DIRS "."
                 "${CMAKE_SOURCE_DIR}/include"
                 "${CMAKE_SOURCE_DIR}/thirdparty/cJSON"
    REQUIRES logger spiffs nvs_flash esp_partition app_update sha256
)
