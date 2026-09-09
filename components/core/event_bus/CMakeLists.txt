# Event Bus 组件 CMakeLists.txt
#
# 事件总线模块，提供模块间的发布-订阅通信机制
# event_bus_test.c / example_usage.c 为 ESP 端遗留测试（C8 项），已移出固件构建，
# 待迁入 tests/ 作 PC 门禁用例

idf_component_register(
    SRCS "event_bus.c" "event_bus_port.c"
    INCLUDE_DIRS "."
    REQUIRES mem_pool freertos logger
    PRIV_REQUIRES esp_timer
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_CORE)
