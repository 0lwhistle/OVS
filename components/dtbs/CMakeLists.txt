idf_component_register(
    SRCS "dtree.c" 
         "${CMAKE_SOURCE_DIR}/thirdparty/cJSON/cJSON.c"
    INCLUDE_DIRS "." 
                 "${CMAKE_SOURCE_DIR}/include"
                 "${CMAKE_SOURCE_DIR}/thirdparty/cJSON"
    REQUIRES logger spiffs
)
