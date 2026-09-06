# ESP32-S3 桌面智能助手

一个基于ESP32-S3的桌面智能助手，集成LoRa通讯、音频处理、LVGL图形界面等功能。

## 项目特性

- ✅ **LVGL图形界面** - 流畅的触控交互
- ✅ **LoRa远程通讯** - 3km对讲机+留言功能
- ✅ **WiFi+蓝牙配网** - 便捷的网络配置
- ✅ **W25Q128 Flash存储** - 留言和数据存储
- ✅ **音频处理** - 录音和播放
- ✅ **Web上位机** - 远程监控和管理
- ✅ **电池供电** - 便携使用

## 硬件清单

| 模块 | 型号 | 价格 |
|------|------|------|
| ESP32-S3核心板 | ESP32-S3-R16N8 | ¥40 |
| 18650充放电模块 | TP4056+升压 | ¥30 |
| W25Q128 Flash模块 | Micro SD SPI | ¥25 |
| 麦克风 | INMP441 I2S | ¥20 |
| 扬声器功放 | MAX98357A I2S | ¥25 |
| 显示屏 | ST7789+CST816S | ¥30 |
| LoRa模块 | SX1278 433MHz | ¥30 |
| **总计** | | **¥200** |

## 目录结构

```
diy-smart-assistant/
├── firmware/                    # 固件源码
│   ├── CMakeLists.txt          # 主CMake配置
│   ├── main/                   # 主程序
│   │   ├── CMakeLists.txt      # 组件CMake配置
│   │   ├── main.c              # 主入口
│   │   ├── include/            # 头文件
│   │   └── components/         # 组件
│   └── components/             # 自定义组件
│       ├── lora_manager/       # LoRa管理
│       ├── audio_manager/      # 音频管理
│       ├── display_manager/    # 显示管理
│       ├── flash_manager/         # W25Q128 Flash管理
│       ├── wifi_manager/       # WiFi管理
│       ├── ble_manager/        # 蓝牙管理
│       ├── web_server/         # Web服务器
│       └── ui_manager/         # UI管理
├── hardware/                   # 硬件设计
│   ├── schematic/              # 原理图
│   ├── pcb/                    # PCB设计
│   └── bom/                    # 物料清单
├── docs/                       # 文档
│   ├── design.md               # 设计文档
│   ├── api.md                  # API文档
│   └── user_manual.md          # 用户手册
├── tools/                      # 工具
│   ├── flash.sh                # 烧录脚本
│   ├── monitor.sh              # 监控脚本
│   └── build.sh                # 编译脚本
└── README.md                   # 本文件
```

## 快速开始

### 1. 环境准备

```bash
# 安装ESP-IDF
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
source export.sh

# 克隆项目
git clone https://github.com/yourusername/diy-smart-assistant.git
cd diy-smart-assistant/firmware
```

### 2. 编译固件

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 编译
idf.py build

# 烧录
idf.py -p /dev/ttyUSB0 flash

# 监控
idf.py -p /dev/ttyUSB0 monitor
```

### 3. 硬件连接

参考 `hardware/` 目录中的原理图和接线图。

## 开发计划

### 第1周: 环境搭建与基础驱动
- [ ] ESP-IDF环境配置
- [ ] 核心板基础测试
- [ ] SPI显示屏驱动
- [ ] LVGL移植
- [ ] W25Q128 Flash驱动

### 第2周: 音频与LoRa
- [ ] I2S麦克风驱动
- [ ] I2S功放驱动
- [ ] 音频编解码
- [ ] SX1278 LoRa驱动
- [ ] LoRa通讯测试

### 第3周: 功能实现
- [ ] LoRa对讲功能
- [ ] 留言功能
- [ ] 蓝牙配网
- [ ] Mongoose Web服务器
- [ ] Web上位机

### 第4周: UI与集成
- [ ] LVGL主界面
- [ ] 功能菜单
- [ ] 系统集成
- [ ] 电源管理
- [ ] 测试与调试

## 技术栈

- **MCU**: ESP32-S3 (Xtensa LX7 双核 240MHz)
- **操作系统**: FreeRTOS
- **开发框架**: ESP-IDF v5.x
- **图形库**: LVGL v8/v9
- **Web服务器**: Mongoose
- **文件系统**: FATFS
- **无线协议**: WiFi, BLE, LoRa

## 参考资源

- [ESP-IDF官方文档](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/)
- [LVGL官方文档](https://docs.lvgl.io/)
- [Mongoose文档](https://mongoose.ws/)
- [SX1278数据手册](https://www.semtech.com/products/wireless-rf/lora-connectivity/sx1278)

## 许可证

MIT License

## 联系方式

如有问题，请提交Issue或联系开发者。
