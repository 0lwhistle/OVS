# OVS 项目目录结构

## 概述

本项目采用 **组件化架构**，遵循 ESP-IDF 标准目录结构，实现模块化设计和接口隔离。

## 目录结构

```
ovs/
├── components/              # ESP-IDF 组件目录
│   ├── api/                 # 公共 API 层
│   │   ├── tasker/         #   - 任务调度器 API
│   │   └── event_bus/      #   - 事件总线 API
│   ├── core/               # 核心基础设施
│   │   ├── tasker/         #   - 任务调度器实现
│   │   ├── event_bus/      #   - 事件总线实现
│   │   └── logger/         #   - 日志系统实现
│   ├── drivers/            # 硬件驱动
│   │   ├── wifi/           #   - WiFi 驱动
│   │   ├── led/            #   - LED 驱动
│   │   ├── beep/           #   - 蜂鸣器驱动
│   │   ├── gpio/           #   - GPIO 驱动
│   │   └── sr04/           #   - 超声波传感器驱动
│   └── modules/            # 功能模块
│       ├── ota/            #   - OTA 升级模块
│       ├── web/            #   - Web 服务器模块
│       └── heartbeat/      #   - 心跳监控模块
├── config/                 # 配置文件
│   ├── sdkconfig           #   - ESP-IDF 配置
│   ├── sdkconfig.old       #   - 旧配置备份
│   └── partitions.csv      #   - 分区表
├── docs/                   # 文档目录
│   ├── ARCHITECTURE.md     #   - 架构文档
│   └── PROJECT_STRUCTURE.md#   - 项目结构说明
├── include/                # 公共头文件目录
│   ├── tasker.h            #   - 任务调度器公共 API
│   ├── event_bus.h         #   - 事件总线公共 API
│   ├── logger.h            #   - 日志系统公共 API
│   ├── heartbeat.h         #   - 心跳监控公共 API
│   ├── ota.h               #   - OTA 升级公共 API
│   └── web.h               #   - Web 服务器公共 API
├── scripts/                # 脚本目录
│   ├── mybuild.sh          #   - 构建脚本
│   ├── burn.sh             #   - 烧录脚本
│   ├── ota_update.sh       #   - OTA 更新脚本
│   ├── toBoard.sh          #   - 部署脚本
│   ├── fs_to_c.py          #   - 文件系统转 C 数组
│   ├── pack_web.py         #   - Web 资源打包
│   └── sign_firmware.py    #   - 固件签名
├── src/                    # 源代码目录
│   ├── main/               #   - 主程序入口
│   └── app/                #   - 应用代码
│       └── diy-smart-assistant/ # - 智能助手应用
├── web/                    # Web 前端源码
│   └── vue-ui/             #   - Vue.js 前端
└── CMakeLists.txt          # 项目 CMake 配置
```

## 设计原则

### 1. 模块化设计

- **api/**: 对外提供的公共 API，其他模块通过 API 层访问功能
- **core/**: 核心基础设施，提供底层服务
- **drivers/**: 硬件驱动，封装硬件操作
- **modules/**: 功能模块，实现具体业务逻辑

### 2. 接口隔离

- **include/**: 只包含公共头文件，定义对外接口
- 组件内部头文件放在各自目录中，不对外暴露
- 通过 `CMakeLists.txt` 的 `INCLUDE_DIRS` 控制可见性

### 3. 依赖关系

```
app/main
    ↓
api (tasker, event_bus)
    ↓
core (tasker, event_bus, logger)
    ↓
drivers (wifi, led, beep, gpio, sr04)
    ↓
modules (ota, web, heartbeat)
```

## 使用方法

### 构建项目

```bash
# 使用构建脚本
./scripts/mybuild.sh

# 或者直接使用 idf.py
idf.py build
```

### 烧录固件

```bash
# 使用烧录脚本
./scripts/burn.sh

# 或者直接使用 idf.py
idf.py -p /dev/ttyUSB0 flash
```

### 添加新组件

1. 在 `components/` 下创建新目录
2. 创建 `CMakeLists.txt` 定义组件
3. 在 `include/` 下添加公共头文件
4. 更新根 `CMakeLists.txt` 的 `EXTRA_COMPONENT_DIRS`

## 注意事项

1. **不要直接修改 `components/` 下的内部头文件**，只使用 `include/` 下的公共 API
2. **新增功能模块** 需要同时提供公共头文件和实现文件
3. **依赖关系** 需要在 `CMakeLists.txt` 中正确声明
