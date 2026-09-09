# ESP-IDF 项目 CMakeLists.txt
# 项目名称: ovs (ESP32-S3 智能助手)
cmake_minimum_required(VERSION 3.22)

# 分区表配置
set(PARTITION_TABLE_CUSTOM_FILENAME "partitions.csv")
set(PARTITION_TABLE_CUSTOM y)

# 组件目录
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/components/api/tasker_api
    ${CMAKE_SOURCE_DIR}/components/api/eventbus_api
    ${CMAKE_SOURCE_DIR}/components/core/tasker
    ${CMAKE_SOURCE_DIR}/components/core/event_bus
    ${CMAKE_SOURCE_DIR}/components/core/logger
    ${CMAKE_SOURCE_DIR}/components/dtbs
    ${CMAKE_SOURCE_DIR}/components/core/ovs_vfs
    ${CMAKE_SOURCE_DIR}/components/drivers/wifi
    ${CMAKE_SOURCE_DIR}/components/drivers/led
    ${CMAKE_SOURCE_DIR}/components/drivers/gpio
    ${CMAKE_SOURCE_DIR}/components/drivers/i2s_drv
    ${CMAKE_SOURCE_DIR}/components/drivers/i2c_drv
    ${CMAKE_SOURCE_DIR}/components/drivers/spi_drv
    ${CMAKE_SOURCE_DIR}/components/drivers/uart_drv
    ${CMAKE_SOURCE_DIR}/components/modules/ota
    ${CMAKE_SOURCE_DIR}/components/modules/net_mgr
    ${CMAKE_SOURCE_DIR}/components/modules/web
    ${CMAKE_SOURCE_DIR}/components/modules/heartbeat
    ${CMAKE_SOURCE_DIR}/components/modules/audio_module
    ${CMAKE_SOURCE_DIR}/components/modules/st7789
    ${CMAKE_SOURCE_DIR}/components/modules/aht30
    ${CMAKE_SOURCE_DIR}/components/modules/cst816s
    ${CMAKE_SOURCE_DIR}/components/modules/lora
    ${CMAKE_SOURCE_DIR}/components/modules/w25q128
    ${CMAKE_SOURCE_DIR}/components/modules/internal_flash
    ${CMAKE_SOURCE_DIR}/components/modules/holder
    ${CMAKE_SOURCE_DIR}/thirdparty/sha256
    ${CMAKE_SOURCE_DIR}/thirdparty/lvgl_lib
    ${CMAKE_SOURCE_DIR}/src/lvgl
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(ovs)

# 将 include/ 目录添加到所有组件的 include 路径
idf_build_set_property(INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/include" APPEND)

# SPIFFS 镜像生成（用于设备树配置）
spiffs_create_partition_image(spiffs components/dtbs/config FLASH_IN_PROJECT)

# 设备树 A/B 槽容器（OTA 上传用，pack_dtb.py 生成 build/dtb.bin）
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/dtb.bin
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/pack_dtb.py
            ${CMAKE_SOURCE_DIR}/components/dtbs/config/ovs.dtb.json
            ${CMAKE_BINARY_DIR}/dtb.bin
    DEPENDS ${CMAKE_SOURCE_DIR}/components/dtbs/config/ovs.dtb.json
    COMMENT "Packing device tree container (dtb.bin)"
)
add_custom_target(dtb_bin ALL DEPENDS ${CMAKE_BINARY_DIR}/dtb.bin)
