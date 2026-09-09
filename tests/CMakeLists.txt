# OVS PC 宿主测试（core 纯逻辑模块的单元/压力测试，不在固件内）
# 用法: cmake -S tests -B build-tests && cmake --build build-tests && ./build-tests/ovs_tests
cmake_minimum_required(VERSION 3.16)
project(ovs_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(OVS_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

add_executable(ovs_tests
    test_mem.c
    ${OVS_ROOT}/components/core/mem_pool/mem.c
    ${OVS_ROOT}/components/core/mem_pool/mem_pool.c
    ${OVS_ROOT}/components/core/logger/logger.c
)
target_include_directories(ovs_tests PRIVATE
    ${OVS_ROOT}/components/core/mem_pool
    ${OVS_ROOT}/components/core/logger
)
find_package(Threads REQUIRED)
target_link_libraries(ovs_tests PRIVATE Threads::Threads)
