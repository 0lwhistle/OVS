# OVS - ESP32-S3 智能助手项目

## 项目概述

OVS 是一个基于 ESP32-S3 的智能助手项目，采用组件化架构设计，支持 WiFi、OTA 升级、Web 管理等功能。

## 快速开始

### 环境要求

- ESP-IDF v6.x
- Python 3.8+
- Node.js 16+ (用于 Web 前端开发)

### 构建项目

```bash
# 使用构建脚本 (推荐)
./scripts/mybuild.sh

# 或者手动构建
idf.py build
```

### 烧录固件

```bash
# 使用烧录脚本
./scripts/burn.sh

# 或指定串口
./scripts/burn.sh /dev/ttyUSB0

# 或手动烧录
idf.py -p /dev/ttyACM0 flash monitor
```

### OTA 更新

```bash
# 使用 OTA 更新脚本
./scripts/ota_update.sh <esp32-ip>

# 示例
./scripts/ota_update.sh 192.168.1.100
```

## 项目结构

```
ovs/
├── components/              # ESP-IDF 组件
│   ├── api/                 #   公共 API 层 (tasker_api, eventbus_api)
│   ├── core/                #   核心基础设施 (tasker, event_bus, logger)
│   ├── drivers/             #   硬件驱动 (wifi, led, beep, gpio, sr04)
│   └── modules/             #   功能模块 (ota, web, heartbeat)
├── config/                  # 配置文件 (sdkconfig, partitions.csv)
├── docs/                    # 项目文档
├── include/                 # 公共头文件 (只暴露接口，隐藏实现)
├── main/                    # 主程序入口
├── scripts/                 # 工具脚本 (构建、烧录、OTA、签名)
├── src/                     # 源代码
│   └── app/                 #   应用代码 (diy-smart-assistant)
├── web/                     # Web 前端源码 (vue-ui)
└── CMakeLists.txt           # 项目 CMake 配置
```

详细结构说明请参考 [docs/PROJECT_STRUCTURE.md](docs/PROJECT_STRUCTURE.md)

## 脚本说明

| 脚本 | 说明 |
|------|------|
| `scripts/mybuild.sh` | 完整构建流程：编译 Vue 前端 + 打包 Web 资源 + 编译固件 + 签名 |
| `scripts/burn.sh` | 烧录固件到 ESP32 |
| `scripts/toBoard.sh` | 一键构建并烧录 |
| `scripts/ota_update.sh` | OTA 远程升级 (支持断点续传) |
| `scripts/sign_firmware.py` | 固件签名工具 |
| `scripts/fs_to_c.py` | Web 资源转 C 数组 |
| `scripts/pack_web.py` | Web 资源打包为 tar (用于网页 OTA) |

## 主要功能

- **WiFi 管理**: STA 模式、热点扫描、自动切换
- **OTA 升级**: 分块传输、SHA256 校验、自动回退
- **Web 管理**: REST API、静态文件服务、SPIFFS 存储
- **任务调度**: 优先级调度、超时控制、自适应队列
- **事件总线**: 发布-订阅机制、异步处理 (待实现)
- **日志系统**: 彩色输出、多级别日志

## 开发指南

### 添加新模块

1. 在 `components/` 下创建新目录
2. 创建 `CMakeLists.txt` 定义组件
3. 在 `include/` 下添加公共头文件
4. 更新根 `CMakeLists.txt` 的 `EXTRA_COMPONENT_DIRS`
5. 更新 `main/CMakeLists.txt` 的 `REQUIRES`

### 代码规范

- 使用 C99 标准
- 遵循 ESP-IDF 编码规范
- 公共 API 使用 `include/` 下的头文件
- 内部实现放在组件目录中

## 文档

- [架构设计](docs/ARCHITECTURE.md)
- [项目结构](docs/PROJECT_STRUCTURE.md)

## 许可证

MIT License
