# OVS 项目目录结构

## 概述

本项目采用 **组件化架构**，模仿 Linux 设备树模型管理硬件配置，遵循 ESP-IDF 标准目录结构。

## 目录结构

```
ovs/
├── components/                  # ESP-IDF 组件目录
│   ├── api/                     # 公共 API 层
│   │   ├── tasker_api/          #   - 任务调度器 API
│   │   └── eventbus_api/        #   - 事件总线 API
│   ├── core/                    # 核心基础设施
│   │   ├── tasker/              #   - 任务调度器实现
│   │   ├── event_bus/           #   - 事件总线实现
│   │   ├── logger/              #   - 日志系统
│   │   └── ovs_vfs/             #   - VFS 虚拟文件系统
│   ├── dtbs/                    # 设备树模块
│   │   ├── dtree.c              #   - 设备树解析器（公共API在 include/dtree.h）
│   │   ├── dtb_ab.{h,c}         #   - 设备树 A/B 裸分区管理（OTA 用）
│   │   └── config/
│   │       └── ovs.dtb.json     #     - 单棵树设备树配置（SPIFFS 镜像源）
│   ├── esp_littlefs/            # LittleFS 组件（vendor 目录）
│   ├── drivers/                 # 硬件驱动
│   │   ├── wifi/                #   - WiFi 驱动（esp_wifi 纯封装，无策略）
│   │   ├── led/                 #   - LED 驱动
│   │   ├── gpio/                #   - GPIO 驱动
│   │   ├── spi_drv/             #   - SPI 驱动（共享计数，多设备共总线）
│   │   ├── i2c_drv/             #   - I2C 驱动
│   │   ├── i2s_drv/             #   - I2S 驱动
│   │   └── uart_drv/            #   - UART 驱动
│   └── modules/                 # 功能模块
│       ├── net_mgr/             #   - 网络管理器（STA/AP 状态机、凭据持久化、配网接口、热切换）
│       ├── ota/                 #   - OTA 升级模块（流式、回滚保护）
│       ├── web/                 #   - Web 服务器（路由注册表、流式上传、WS 推送）
│       ├── heartbeat/           #   - 心跳监控
│       ├── holder/              #   - 全局硬件模块注册表
│       ├── st7789/              #   - ST7789 显示屏
│       ├── w25q128/             #   - W25Q128 NOR Flash（含 VFS 适配）
│       ├── internal_flash/      #   - 内部 Flash VFS 适配
│       ├── lora/                #   - LoRa 无线模块
│       ├── audio_module/        #   - 音频模块
│       ├── aht30/               #   - AHT30 温湿度传感器
│       └── cst816s/             #   - CST816S 触摸屏
├── thirdparty/                  # 第三方库目录
│   ├── cJSON/                   #   - cJSON JSON 解析库
│   ├── mongoose/                #   - Mongoose Web 服务器
│   └── sha256/                  #   - SHA256（OTA 校验）
├── docs/                        # 项目文档
│   ├── ARCHITECTURE.md          #   - 架构文档
│   ├── PROJECT_STRUCTURE.md     #   - 项目结构 (本文件)
│   ├── ota_guide.md             #   - OTA 使用指南
│   ├── development_log.md       #   - 开发日志（新条目在顶部）
│   ├── development_log_net_ota.md # - 网络/OTA 开发日志
│   ├── handoff_summary.md       #   - 跨会话交接摘要
│   ├── peripheral_drivers_summary.md # - 外设驱动总结
│   └── diy-smart-assistant/     #   - DIY 教程文档集
├── include/                     # 公共头文件
│   ├── tasker.h                 #   - 任务调度器 API
│   ├── dtree.h                  #   - 设备树 API
│   ├── event_bus.h              #   - 事件总线 API
│   ├── logger.h                 #   - 日志 API
│   ├── heartbeat.h              #   - 心跳 API
│   ├── ota.h                    #   - OTA API
│   ├── web.h                    #   - Web API
│   └── lvgl.h                   #   - LVGL 头（临时占位）
├── main/                        # main 组件垫片（IDF 要求组件名为 main；
│                                #   仅 CMakeLists 注册 src/app 的源文件）
├── scripts/                     # 工具脚本
├── src/app/                     # 应用代码（main.c、vfs_stress 等）
├── web/                         # Web 前端源码
├── CMakeLists.txt               # 项目 CMake 配置
├── sdkconfig                    # ESP-IDF 配置
├── partitions.csv               # 分区表
└── README.md
```

## 设备树设计

### 设计理念

模仿 Linux 设备树模型，使用**单棵嵌套 JSON 树**（`ovs.dtb.json`）描述硬件配置：
- **配置与代码分离**: 修改引脚只需改 JSON，无需重编译（可走 A/B 槽 OTA）
- **嵌套表达挂载**: 设备节点嵌套在总线节点下（如 `buses.spi2.flash`），父子关系即挂载关系
- **compatible 绑定**: 模块以 `dtree_find_by_compatible()` 定位自己的节点，代码零硬编码路径
- **统一管理**: 所有硬件配置集中存放

### JSON 配置示例（ovs.dtb.json 节选）

```json
{
    "compatible": "ovs,esp32s3-smart-assistant",
    "buses": {
        "i2s0": {
            "compatible": "esp32s3-i2s",
            "description": "I2S 总线 (麦克风 + 功放)",
            "bclk_pin": 6,
            "ws_pin": 5,
            "sample_rate_hz": 16000,

            "microphone": {
                "compatible": "i2s-microphone",
                "data_in_pin": 7
            }
        }
    }
}
```

### 使用方法

```c
#include "dtree.h"

// 初始化设备树（A/B 槽优先，失败回退 /spiffs/ovs.dtb.json）
dtree_init();

// 按 compatible 定位设备节点（推荐），父节点即所属总线
dtree_node_t* mic = dtree_find_by_compatible("i2s-microphone");
int32_t din_pin;
dtree_get_int(mic, "data_in_pin", &din_pin);

// 或通过完整路径读取属性（任意深度）
int32_t bclk;
DTREE_INT("buses.i2s0", "bclk_pin", &bclk);
```

## 组件依赖关系

```
main（垫片，注册 src/app 源文件）
├── tasker_api ──→ tasker ──→ esp_driver_gptimer, esp_timer, logger
├── dtbs ──→ logger, spiffs, cJSON (thirdparty), sha256 (thirdparty), nvs_flash, esp_partition, app_update
├── wifi ──→ esp_wifi, esp_event, esp_netif, esp_timer, logger
├── led ──→ esp_driver_gpio, gpio
├── ota ──→ app_update, esp_partition, esp_app_format, esp_system, esp_timer, freertos, logger, sha256 (thirdparty), dtbs
├── net_mgr ──→ wifi, dtbs, nvs_flash, espressif__mdns, esp_wifi, esp_event, esp_netif, esp_timer, freertos, event_bus, logger
├── web ──→ spiffs, freertos, ota, net_mgr, dtbs, wifi, heartbeat, tasker, logger, esp_timer, mongoose (thirdparty)
└── heartbeat ──→ esp_wifi, esp_timer, freertos, tasker, tasker_api, logger
```

## 第三方库

| 库 | 路径 | 用途 |
|---|------|------|
| cJSON | `thirdparty/cJSON/` | JSON 解析，用于设备树 |
| mongoose | `thirdparty/mongoose/` | Web 服务器 |

## 命名规范

| 类型 | 命名规则 | 示例 |
|------|---------|------|
| 组件目录 | 小写 + 下划线 | `tasker_api`, `dtbs` |
| 设备树配置 | 小写 + 点分 | `ovs.dtb.json` |
| 头文件 | 小写 + 下划线 | `dtree.h`, `tasker.h` |
| 函数名 | 小写 + 下划线 | `dtree_init()`, `dtree_get_int()` |
| 宏定义 | 大写 + 下划线 | `DTREE_INT()`, `TASK_OK` |

## 构建命令

```bash
# 完整构建
./scripts/mybuild.sh

# 清理后重新构建
./scripts/mybuild.sh --clean

# 烧录
./scripts/burn.sh

# OTA 升级（免串口日常迭代，详见 docs/ota_guide.md）
./scripts/ota_push.sh [ovs.local|IP]

# 环境初始化（之后脚本与 idf 命令免路径）+ 一键发布
source scripts/env.sh
./scripts/ovs_release          # 只完整构建
./scripts/ovs_release --ota [IP]  # 构建后 OTA 推送
```

## 添加新设备

1. 在 `components/dtbs/config/ovs.dtb.json` 中，把设备节点嵌套到对应总线节点下
2. 为节点写明 `compatible` 与引脚/参数属性
3. 在驱动/模块中用 `dtree_find_by_compatible()` 定位节点并读取配置

```c
// 示例：模块内定位新设备节点（父节点即所属总线）
dtree_node_t* dev = dtree_find_by_compatible("my-sensor");
int32_t pin;
dtree_get_int(dev, "data_pin", &pin);
```

## LVGL UI 模块

### 目录结构

> 当前状态：六层目录为脚手架（各层目录暂只含 README 与设计约定），
> 仅 `lvgl_app.c` 参与编译（见 `src/lvgl/CMakeLists.txt` 的 TODO 列表）。

```
src/lvgl/
├── lvgl_app.c/h         # LVGL 应用层主文件（当前唯一参与编译的源文件）
├── lv_conf.h            # LVGL 配置文件
├── ui/                  # 控件层（脚手架）
├── widgets/             # 窗口层（脚手架）
├── pages/               # 页面层（脚手架）
├── navigator/           # 导航层（脚手架）
├── presenters/          # 展示器层（脚手架）
├── bridge/              # 桥接层（脚手架）
├── fonts/               # 字体文件
└── assets/              # 资源文件 (图片、图标)
```

### 使用方法

```c
#include "lvgl_app.h"

// 初始化
lvgl_app_init();

// 在任务循环中处理（内部加锁保护 LVGL）
while (1) {
    lvgl_app_handler(10);
}

// 背光亮度
lvgl_app_set_brightness(128);

// 其他任务里安全操作 LVGL 对象
lvgl_app_lock();
/* ... LVGL 操作 ... */
lvgl_app_unlock();
```

### 添加新页面 / 自定义控件

按下方"LVGL UI 模块 (六层架构)"的分层约定实现：页面放 `pages/`、
窗口放 `widgets/`、控件放 `ui/`，经 `navigator/` 注册，
业务逻辑在 `presenters/`、后端对接在 `bridge/`。

## LVGL UI 模块 (六层架构)

### 架构图

```
┌─────────────────────────────────────────────────────────────┐
│                        pages (页面层)                         │
│   组合 ui 控件和 widgets 窗口，形成完整的用户界面页面              │
├─────────────────────────────────────────────────────────────┤
│                      widgets (窗口层)                         │
│   由 ui 控件组成的最小可操作界面（对话框、输入框、键盘等）          │
├─────────────────────────────────────────────────────────────┤
│                         ui (控件层)                           │
│   自定义控件的纯 UI 实现，只提供回调接口，不关心业务逻辑           │
├─────────────────────────────────────────────────────────────┤
│                      navigator (导航层)                       │
│   管理页面注册、父子关系、页面栈、当前显示页面                     │
├─────────────────────────────────────────────────────────────┤
│               presenters (展示器层) + bridge (桥接层)          │
│   presenters: 实现具体页面/窗口/控件的功能和回调                  │
│   bridge: 对接后端程序和硬件驱动，实现业务与 UI 解耦              │
└─────────────────────────────────────────────────────────────┘
```

### 目录结构

```
src/lvgl/
├── ui/                          # 控件层 - 纯 UI 实现
│   ├── base/                    #   基础控件
│   ├── controls/                #   可操作控件
│   ├── indicators/              #   指示控件
│   └── containers/              #   布局容器
├── widgets/                     # 窗口层 - 最小可操作界面
│   ├── dialog/                  #   对话框
│   ├── input/                   #   输入窗口
│   ├── keyboard/                #   键盘窗口
│   └── panel/                   #   面板窗口
├── pages/                       # 页面层 - 完整界面
│   ├── home/                    #   主页
│   ├── settings/                #   设置页
│   ├── lora/                    #   LoRa 页
│   ├── audio/                   #   音频页
│   └── about/                   #   关于页
├── navigator/                   # 导航层 - 页面管理
├── bridge/                      # 桥接层 - 业务对接
├── presenters/                  # 展示器层 - 功能实现
│   ├── pages/                   #   页面展示器
│   ├── widgets/                 #   窗口展示器
│   └── controls/                #   控件展示器
├── fonts/                       # 字体
└── assets/                      # 资源文件
```

### 各层职责

| 层 | 职责 | 原则 |
|----|------|------|
| ui | 实现控件纯 UI | 只关心外观和交互，不关心业务 |
| widgets | 组合控件成窗口 | 用户直接操作的最小界面 |
| pages | 组合控件和窗口 | 形成功能完整的页面 |
| navigator | 管理页面导航 | 页面栈、父子关系、切换动画 |
| bridge | 对接后端 | 屏蔽实现细节，统一数据接口 |
| presenters | 实现业务逻辑 | 实现回调，连接 UI 和后端 |
