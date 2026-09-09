idf_component_register(
    SRCS "dtree.c"
         "dtb_ab.c"
         "${CMAKE_SOURCE_DIR}/thirdparty/cJSON/cJSON.c"
    INCLUDE_DIRS "."
                 "${CMAKE_SOURCE_DIR}/include"
                 "${CMAKE_SOURCE_DIR}/thirdparty/cJSON"
    REQUIRES logger mem_pool spiffs nvs_flash esp_partition app_update sha256
)

# mem_pool 模块标签：dtbs 全部分配记入 DTREE 桶
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_DTREE)
