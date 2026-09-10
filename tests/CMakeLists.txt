# OVS PC 宿主测试（core 纯逻辑模块的单元/压力测试，不在固件内）
# 用法: cmake -S tests -B build-tests && cmake --build build-tests && ./build-tests/ovs_tests
# 覆盖: mem_pool / event_bus / tasker / audio T2（mock i2s）/ navigator 栈纯逻辑

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
    test_audio.c
    test_nav_stack.c

    # 被测 core 组件
    ${OVS_ROOT}/components/core/mem_pool/mem.c
    ${OVS_ROOT}/components/core/mem_pool/mem_pool.c
    ${OVS_ROOT}/components/core/event_bus/event_bus.c
    ${OVS_ROOT}/components/core/event_bus/event_bus_port.c
    ${OVS_ROOT}/components/core/tasker/task_manager.c
    ${OVS_ROOT}/components/core/tasker/task_worker.c
    ${OVS_ROOT}/components/core/tasker/tasker_port.c
    ${OVS_ROOT}/components/api/tasker_api/tasker.c
    ${OVS_ROOT}/components/core/logger/logger.c

    # 音频 T2（[GUI]）：真实模块 + mock 依赖
    ${OVS_ROOT}/components/modules/audio_module/audio_module.c
    ${OVS_ROOT}/components/modules/audio_module/audio_port.c
    ${OVS_ROOT}/components/modules/audio_player/audio_player.c
    ${OVS_ROOT}/components/modules/audio_player/audio_player_port_stub.c
    i2s_mock.c
    dtree_mock.c

    # navigator 栈纯逻辑（[GUI] 批次②）
    ${OVS_ROOT}/src/lvgl/navigator/nav_stack.c
)

target_include_directories(ovs_tests PRIVATE
    ${OVS_ROOT}/components/core/mem_pool
    ${OVS_ROOT}/components/core/event_bus
    ${OVS_ROOT}/components/core/tasker
    ${OVS_ROOT}/components/api/tasker_api
    ${OVS_ROOT}/components/core/logger
    ${OVS_ROOT}/components/dtbs
    ${OVS_ROOT}/components/drivers/i2s_drv
    ${OVS_ROOT}/components/modules/audio_module
    ${OVS_ROOT}/components/modules/audio_player
    ${OVS_ROOT}/src/lvgl/navigator
)

# PC 测试视作 ESP 构建（启用相同代码路径：port 层 pthread 分支由非 ESP 走）
target_compile_definitions(ovs_tests PRIVATE EVENT_BUS_TEST_PC=1)

find_package(Threads REQUIRED)
target_link_libraries(ovs_tests PRIVATE Threads::Threads)
