# OVS - ESP32-S3 智能助手项目

## 项目概述

OVS 是一个基于 ESP32-S3 的智能助手项目，采用组件化架构，模仿 Linux 设备树模型管理硬件配置，支持 WiFi、OTA、Web 管理等功能。

## 项目结构

```
ovs/
├── components/              # ESP-IDF 组件
│   ├── api/                 #   公共 API 层
│   ├── core/                #   核心基础设施
│   ├── dtbs/                #   设备树模块
│   ├── drivers/             #   硬件驱动
│   └── modules/             #   功能模块
├── thirdparty/              # 第三方库
│   ├── cJSON/               #   - JSON 解析库
│   └── mongoose/            #   - Web 服务器库
├── config/                  # 配置文件
├── docs/                    # 项目文档
├── include/                 # 公共头文件
├── main/                    # 主程序
├── scripts/                 # 工具脚本
├── src/app/                 # 应用代码
└── web/                     # Web 前端
```

## 快速开始

### 环境要求

- ESP-IDF v6.x
- Node.js 16+

### 构建

```bash
# 完整构建 (Vue 前端 + 固件 + 签名)
./scripts/mybuild.sh

# 清理后重新构建
./scripts/mybuild.sh --clean
```

### 烧录

```bash
./scripts/burn.sh
# 或指定串口
./scripts/burn.sh /dev/ttyUSB0
```

### OTA 升级

```bash
./scripts/ota_update.sh <esp32-ip>
```

## 设备树

项目使用 JSON 配置文件描述硬件，配置文件位于 `components/dtbs/config/`：

| 文件 | 说明 |
|------|------|
| `system.json` | 系统配置 (MCU、调试串口、LED) |
| `i2s.json` | I2S 总线 (麦克风 + 功放) |
| `i2c.json` | I2C 总线 (触控 + 温湿度) |
| `lora.json` | LoRa 无线模块 |
| `spi.json` | SPI 总线 (显示屏 + Flash) |

### 使用示例

```c
#include "dtree.h"

// 初始化
dtree_init();

// 读取引脚配置
int bclk = DTREE_INT("i2s.bus", "bclk_pin", 6);
const char* compat = DTREE_STR("i2s.microphone", "compatible");
```

## 脚本说明

| 脚本 | 说明 |
|------|------|
| `scripts/mybuild.sh` | 完整构建 |
| `scripts/mybuild.sh --clean` | 清理后重新构建 |
| `scripts/burn.sh` | 烧录固件 |
| `scripts/ota_update.sh` | OTA 远程升级 |

## 主要功能

- **设备树**: JSON 配置硬件引脚，配置与代码分离
- **WiFi**: STA 模式、热点扫描、自动切换
- **OTA**: 分块传输、SHA256 校验、断点续传
- **Web**: REST API、静态文件服务
- **任务调度**: 优先级调度、超时控制

## 文档

- [项目结构详解](docs/PROJECT_STRUCTURE.md)
- [架构设计](docs/ARCHITECTURE.md)

## 许可证

MIT License
