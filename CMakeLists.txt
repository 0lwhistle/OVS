# ESP-IDF 项目 CMakeLists.txt
# 项目名称: ovs (ESP32-S3 智能助手)
cmake_minimum_required(VERSION 3.22)

# 组件目录
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/components/api/tasker_api
    ${CMAKE_SOURCE_DIR}/components/api/eventbus_api
    ${CMAKE_SOURCE_DIR}/components/core/tasker
    ${CMAKE_SOURCE_DIR}/components/core/event_bus
    ${CMAKE_SOURCE_DIR}/components/core/logger
    ${CMAKE_SOURCE_DIR}/components/dtbs
    ${CMAKE_SOURCE_DIR}/components/drivers/wifi
    ${CMAKE_SOURCE_DIR}/components/drivers/led
    ${CMAKE_SOURCE_DIR}/components/drivers/gpio
    ${CMAKE_SOURCE_DIR}/components/drivers/i2s_drv
    ${CMAKE_SOURCE_DIR}/components/drivers/i2c_drv
    ${CMAKE_SOURCE_DIR}/components/drivers/spi_drv
    ${CMAKE_SOURCE_DIR}/components/drivers/uart_drv
    ${CMAKE_SOURCE_DIR}/components/modules/ota
    ${CMAKE_SOURCE_DIR}/components/modules/web
    ${CMAKE_SOURCE_DIR}/components/modules/heartbeat
    ${CMAKE_SOURCE_DIR}/components/modules/audio_module
    ${CMAKE_SOURCE_DIR}/components/modules/st7789
    ${CMAKE_SOURCE_DIR}/components/modules/aht30
    ${CMAKE_SOURCE_DIR}/components/modules/cst816s
    ${CMAKE_SOURCE_DIR}/components/modules/lora
    ${CMAKE_SOURCE_DIR}/components/modules/w25q128
    ${CMAKE_SOURCE_DIR}/components/modules/holder
    ${CMAKE_SOURCE_DIR}/src/lvgl
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(ovs)

# 将 include/ 目录添加到所有组件的 include 路径
idf_build_set_property(INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/include" APPEND)
