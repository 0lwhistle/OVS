# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)
set(CMAKE_C_COMPILER_LAUNCHER ccache)
set(CMAKE_CXX_COMPILER_LAUNCHER ccache)

# 关键：告诉 ESP-IDF 去 module/ 下面找额外的组件
set(EXTRA_COMPONENT_DIRS ${CMAKE_SOURCE_DIR}/modules)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

include_directories(
	modules/wifi
	modules/web
	modules/gpio_ctrl
	modules/led_ctrl
	modules/beep_ctrl
	modules/sr04_ctrl
	modules/uart_ctrl
	modules/task_manager
)


project(web)


