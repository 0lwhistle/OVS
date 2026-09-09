# ESP32-S3 桌面智能助手

一个基于ESP32-S3的桌面智能助手，集成LoRa通讯、音频处理、Web管理等功能。

## 项目特性

- ✅ **设备树配置** - JSON 描述硬件，配置与代码分离
- ✅ **LoRa远程通讯** - 3km对讲机+留言功能
- ✅ **Web上位机** - 远程监控和管理
- ✅ **OTA升级** - 远程固件升级
- ✅ **音频处理** - 录音和播放
- ✅ **任务调度** - 优先级调度、超时控制

## 目录结构

```
ovs/
├── components/              # ESP-IDF 组件
│   ├── api/                 #   公共 API 层
│   ├── core/                #   核心基础设施 (tasker, logger, event_bus, ovs_vfs)
│   ├── dtbs/                #   设备树模块
│   ├── drivers/             #   硬件驱动 (wifi, led, gpio, spi/i2c/i2s/uart)
│   └── modules/             #   功能模块 (ota, web, net_mgr, heartbeat, ...)
├── thirdparty/              # 第三方库 (cJSON, mongoose, sha256)
├── include/                 # 公共头文件
├── main/                    # main 组件垫片（应用代码在 src/app/）
├── scripts/                 # 工具脚本
├── docs/                    # 项目文档
└── docs/diy-smart-assistant/  # 本文档集
    ├── docs/                #   应用文档
    ├── hardware/            #   硬件文档
    └── logs/                #   开发日志
```

## 快速开始

```bash
# 完整构建
./scripts/mybuild.sh

# 烧录
./scripts/burn.sh

# OTA 推送
./scripts/ota_push.sh [ovs.local|IP]
```

## 设备树配置

硬件引脚通过 JSON 配置，单棵树文件为 `components/dtbs/config/ovs.dtb.json`
（设备节点嵌套在总线节点下，模块按 compatible 定位）。

## 文档

- [项目结构](../../../docs/PROJECT_STRUCTURE.md)
- [架构设计](../../../docs/ARCHITECTURE.md)
- [接线图](hardware/wiring_diagram.md)
- [开发日志](logs/development_log.md)

## 许可证

MIT License
