# OVS PC 宿主测试（core 纯逻辑模块的单元/压力测试，不在固件内）
# 用法: cmake -S tests -B build-tests && cmake --build build-tests && ./build-tests/ovs_tests
# 覆盖: mem_pool（5.4 前置）/ event_bus（5.1 门禁）/ tasker（5.2 门禁）

cmake_minimum_required(VERSION 3.16)
project(ovs_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(OVS_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

add_executable(ovs_tests
    test_main.c
    test_mem.c
    test_event_bus.c
    test_tasker.c

    ${OVS_ROOT}/components/core/mem_pool/mem.c
    ${OVS_ROOT}/components/core/mem_pool/mem_pool.c

    ${OVS_ROOT}/components/core/event_bus/event_bus.c
    ${OVS_ROOT}/components/core/event_bus/event_bus_port.c

    ${OVS_ROOT}/components/core/tasker/task_manager.c
    ${OVS_ROOT}/components/core/tasker/task_worker.c
    ${OVS_ROOT}/components/core/tasker/tasker_port.c
    ${OVS_ROOT}/components/api/tasker_api/tasker.c

    ${OVS_ROOT}/components/core/logger/logger.c
)

target_include_directories(ovs_tests PRIVATE
    ${OVS_ROOT}/components/core/mem_pool
    ${OVS_ROOT}/components/core/event_bus
    ${OVS_ROOT}/components/core/tasker
    ${OVS_ROOT}/components/api/tasker_api
    ${OVS_ROOT}/components/core/logger
)

# PC 测试视作 ESP 构建（启用相同代码路径：port 层 pthread 分支由非 ESP 走）
target_compile_definitions(ovs_tests PRIVATE EVENT_BUS_TEST_PC=1)

find_package(Threads REQUIRED)
target_link_libraries(ovs_tests PRIVATE Threads::Threads)
