# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)
set(CMAKE_C_COMPILER_LAUNCHER ccache)
set(CMAKE_CXX_COMPILER_LAUNCHER ccache)

# 关键：告诉 ESP-IDF 去 module/ 下面找额外的组件
set(EXTRA_COMPONENT_DIRS 
${CMAKE_SOURCE_DIR}/modules/devices_ctrl/led_ctrl
${CMAKE_SOURCE_DIR}/modules/devices_ctrl/gpio_ctrl
${CMAKE_SOURCE_DIR}/modules/devices_ctrl/beep_ctrl
${CMAKE_SOURCE_DIR}/modules/devices_ctrl/sr04_ctrl
${CMAKE_SOURCE_DIR}/modules/devices_ctrl/wifi
${CMAKE_SOURCE_DIR}/modules/tasker/task_manager
${CMAKE_SOURCE_DIR}/modules/tasker/task_worker
${CMAKE_SOURCE_DIR}/modules/logger
${CMAKE_SOURCE_DIR}/modules/web
${CMAKE_SOURCE_DIR}/API/tasker
)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)


project(ovs)


