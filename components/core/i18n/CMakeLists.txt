# 契约 I1 多语言服务（[HUB]）
idf_component_register(
    SRCS "i18n.c"
         "${CMAKE_SOURCE_DIR}/thirdparty/cJSON/cJSON.c"
    INCLUDE_DIRS "."
                 "${CMAKE_SOURCE_DIR}/thirdparty/cJSON"
    REQUIRES mem_pool logger
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_CORE)
