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
│   ├── esp_littlefs/        #   LittleFS 组件（vendor）
│   └── modules/             #   功能模块
├── thirdparty/              # 第三方库
│   ├── cJSON/               #   - JSON 解析库
│   ├── mongoose/            #   - Web 服务器库
│   └── sha256/              #   - SHA256（OTA 校验）
├── docs/                    # 项目文档
├── include/                 # 公共头文件
├── main/                    # main 组件垫片（应用代码在 src/app/）
├── scripts/                 # 工具脚本
├── src/app/                 # 应用代码
└── web/                     # Web 前端
```

## 快速开始

### 环境要求

- ESP-IDF v6.x
- Node.js 20+

### 构建

```bash
# 完整构建 (Vue 前端 + Web 打包 + 固件 + 串口烧录，默认先清理 build)
./scripts/mybuild.sh

# 只构建不烧录
./scripts/mybuild.sh --no-flash
```

### 烧录

```bash
./scripts/burn.sh
# 或指定串口
./scripts/burn.sh /dev/ttyUSB0
```

### OTA 升级

```bash
./scripts/ota_push.sh [ovs.local|IP]
```

## 设备树

项目使用 JSON 配置文件描述硬件，配置与代码分离。自 2026-09-08 起为**单棵树**，
配置文件为 `components/dtbs/config/ovs.dtb.json`（设备节点嵌套在总线节点下，
如 `buses.spi2.lcd_display`），支持 A/B 分区 OTA 免串口更新。

### 使用示例

```c
#include "dtree.h"

// 初始化
dtree_init();

// 按 compatible 定位设备节点（推荐），父节点即所属总线
dtree_node_t* dev = dtree_find_by_compatible("w25q128-flash");
int32_t cs_pin;
dtree_get_int(dev, "cs_pin", &cs_pin);

// 或通过完整路径读取属性
int32_t sclk;
DTREE_INT("buses.spi2", "sclk_pin", &sclk);
```

## 脚本说明

| 脚本 | 说明 |
|------|------|
| `scripts/mybuild.sh` | 完整构建（Vue 前端 + 固件 + 串口烧录） |
| `scripts/mybuild.sh --no-flash` | 只构建不烧录 |
| `scripts/burn.sh` | 烧录固件 |
| `scripts/ota_push.sh` | OTA 远程推送（免串口日常迭代） |

## 主要功能

- **设备树**: JSON 配置硬件引脚，配置与代码分离；A/B 分区 OTA 免串口更新
- **WiFi**: STA/AP 双模式、热点扫描、配网通道、免重启热切换
- **OTA**: 流式分块写入、SHA256 校验、15s 回滚保护
- **Web**: REST API、静态文件服务、WebSocket 状态推送
- **模块管理**: 全局硬件模块注册表，故障隔离，状态监控
- **任务调度**: 优先级调度、超时控制

## 文档

- [项目结构详解](docs/PROJECT_STRUCTURE.md)
- [架构设计](docs/ARCHITECTURE.md)

## 许可证

MIT License
