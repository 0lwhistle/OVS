cmake_minimum_required(VERSION 3.16)

# 设置项目名称
set(PROJECT_NAME "smart_assistant")
project(${PROJECT_NAME})

# 包含ESP-IDF CMake
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 主项目
project(${PROJECT_NAME})
