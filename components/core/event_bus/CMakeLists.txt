# Event Bus 组件 CMakeLists.txt
# 
# 事件总线模块，提供模块间的发布-订阅通信机制

idf_component_register(
    SRCS "event_bus.c" "event_bus_test.c" "example_usage.c"
    INCLUDE_DIRS "."
    REQUIRES mem_pool freertos esp_timer logger
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_CORE)
