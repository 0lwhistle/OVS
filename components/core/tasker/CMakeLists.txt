# Tasker 组件 CMakeLists.txt
# 三级 worker（little/middle/lots）+ dispatcher + sched 周期调度
# tasker_test.c 为遗留测试（C8 项），已移出固件构建，由 tests/ovs_tests PC 门禁取代

idf_component_register(
    SRCS "task_manager.c" "task_worker.c" "tasker_port.c"
    INCLUDE_DIRS "."
    REQUIRES mem_pool freertos logger
    PRIV_REQUIRES esp_timer
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_CORE)
