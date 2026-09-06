# ESP32-S3 桌面智能助手 - 项目总结

## 项目概述

**项目名称**: ESP32-S3 桌面智能助手  
**开发平台**: ESP32-S3-R16N8  
**项目类型**: 嵌入式系统开发

---

## 核心特性

### 1. LoRa远程通讯
- **实时对讲**: 3km范围内的语音对讲
- **留言功能**: 无人接听时存储留言
- **设备发现**: 自动发现附近的设备

### 2. 网络功能
- **Web上位机**: 基于Mongoose的Web管理界面
- **远程监控**: 通过Web界面监控设备状态
- **OTA升级**: 远程固件升级

### 3. 音频处理
- **录音功能**: 高质量音频采集
- **播放功能**: 清晰的音频播放
- **编解码**: ADPCM压缩，节省存储空间

### 4. 设备树配置
- JSON 描述硬件引脚和配置
- 配置与代码分离
- 易于维护和扩展

---

## 硬件清单

| 模块 | 型号 | 用途 |
|------|------|------|
| ESP32-S3核心板 | ESP32-S3-R16N8 | 主控制器 |
| W25Q128 Flash | 16MB | 数据存储 |
| 麦克风 | INMP441 I2S | 音频采集 |
| 扬声器功放 | MAX98357A I2S | 音频播放 |
| 显示屏 | ST7789 240x280 | 显示 |
| 触控 | CST816S | 触控交互 |
| LoRa模块 | LR22 433MHz | 远程通讯 |
| 温湿度传感器 | AHT30 | 环境监测 |

---

## 技术架构

### 软件架构
```
应用层: LoRa对讲 + Web上位机 + 音频处理
中间件: FreeRTOS + 设备树(dtree) + 任务调度(tasker)
驱动层: WiFi + I2S + SPI + I2C + UART + GPIO
硬件层: ESP32-S3 + SX1278 + INMP441 + MAX98357A + ST7789
```

### 项目结构
```
ovs/
├── components/              # ESP-IDF 组件
│   ├── api/                 #   公共 API 层
│   ├── core/                #   核心基础设施
│   ├── dtbs/                #   设备树模块
│   ├── drivers/             #   硬件驱动
│   └── modules/             #   功能模块
├── thirdparty/              # 第三方库
├── include/                 # 公共头文件
├── main/                    # 主程序
├── scripts/                 # 工具脚本
└── web/                     # Web 前端
```

---

## 项目亮点

### 1. 设备树设计
模仿 Linux 设备树，使用 JSON 描述硬件配置，配置与代码分离。

### 2. 模块化架构
组件化设计，每个模块独立，通过 API 层访问。

### 3. 实时音频通讯
低延迟的语音对讲，高质量的音频处理。

### 4. 完整的产品级功能
Web 管理、OTA 升级、远程监控。

---

## 文档清单

| 文档 | 路径 | 说明 |
|------|------|------|
| 项目结构 | `docs/PROJECT_STRUCTURE.md` | 目录结构详解 |
| 架构设计 | `docs/ARCHITECTURE.md` | 系统架构说明 |
| 接线图 | `hardware/wiring_diagram.md` | 引脚连接图 |
| 硬件说明 | `docs/hardware_notes_for_software.md` | 软件视角的硬件说明 |
| LoRa协议 | `docs/lora_protocol.md` | LoRa 通讯协议 |
| Web接口 | `docs/web_interface.md` | Web 上位机设计 |
| 开发日志 | `logs/development_log.md` | 开发进度记录 |

---

## 构建命令

```bash
# 完整构建
./scripts/mybuild.sh

# 清理后重新构建
./scripts/mybuild.sh --clean

# 烧录
./scripts/burn.sh

# OTA 升级
./scripts/ota_update.sh <esp32-ip>
```

---

*项目结构整理完成: 2026-09-06*
