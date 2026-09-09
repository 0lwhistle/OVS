# OVS PC 模拟器（LVGL UI 的 SDL2 宿主）
# 用法: scripts/sim_build.sh [--run]
# 与 ESP 端共享: src/lvgl/navigator|pages|bridge 头与实现、src/lvgl/lv_conf.h、
#                components/core/logger（纯 printf 实现，可直接复用）
# 不编译: src/lvgl/lvgl_app.c、src/lvgl/port/*（ESP 专用）

cmake_minimum_required(VERSION 3.16)
project(ovs_sim C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(OVS_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# ---------- SDL2 ----------
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    set(SDL2_LINK_TARGET PkgConfig::SDL2)
else()
    set(SDL2_LINK_TARGET SDL2::SDL2)
endif()

# ---------- LVGL（上游 CMake，配置指向共享 lv_conf.h） ----------
set(LV_BUILD_CONF_DIR ${OVS_ROOT}/src/lvgl CACHE PATH "lv_conf.h dir" FORCE)
set(LV_BUILD_USE_KCONFIG OFF CACHE BOOL "" FORCE)
add_subdirectory(${OVS_ROOT}/thirdparty/lvgl-9.5.0 lvgl_build)

# 双平台开关：lv_conf.h 据此启用 LV_USE_SDL；SDL 头给 lvgl 的 sdl 驱动源码
target_compile_definitions(lvgl PUBLIC OVS_SIMULATOR=1)
target_link_libraries(lvgl PUBLIC ${SDL2_LINK_TARGET})

# ---------- 模拟器可执行 ----------
add_executable(ovs_sim
    main.c
    bridge_time_sim.c
    ${OVS_ROOT}/src/lvgl/navigator/nav.c
    ${OVS_ROOT}/src/lvgl/pages/page_home.c
    ${OVS_ROOT}/components/core/logger/logger.c
)
target_include_directories(ovs_sim PRIVATE
    ${OVS_ROOT}/src/lvgl
    ${OVS_ROOT}/src/lvgl/navigator
    ${OVS_ROOT}/src/lvgl/pages
    ${OVS_ROOT}/src/lvgl/bridge
    ${OVS_ROOT}/components/core/logger
)
target_compile_definitions(ovs_sim PRIVATE OVS_SIMULATOR=1)
target_link_libraries(ovs_sim PRIVATE lvgl)
