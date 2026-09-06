# OVS 项目目录结构

## 概述

本项目采用 **组件化架构**，模仿 Linux 设备树模型管理硬件配置，遵循 ESP-IDF 标准目录结构。

## 目录结构

```
ovs/
├── components/                  # ESP-IDF 组件目录
│   ├── api/                     # 公共 API 层
│   │   ├── tasker_api/          #   - 任务调度器 API
│   │   └── eventbus_api/        #   - 事件总线 API (待实现)
│   ├── core/                    # 核心基础设施
│   │   ├── tasker/              #   - 任务调度器实现
│   │   ├── event_bus/           #   - 事件总线实现 (待实现)
│   │   └── logger/              #   - 日志系统
│   ├── dtbs/                    # 设备树模块 (新增)
│   │   ├── dtree.c              #   - 设备树解析器
│   │   └── config/              #   - JSON 配置文件
│   │       ├── system.json      #     - 系统配置
│   │       ├── i2s.json         #     - I2S 总线配置
│   │       ├── i2c.json         #     - I2C 总线配置
│   │       ├── lora.json        #     - LoRa 模块配置
│   │       └── spi.json         #     - SPI 总线配置
│   ├── drivers/                 # 硬件驱动
│   │   ├── wifi/                #   - WiFi 驱动
│   │   ├── led/                 #   - LED 驱动
│   │   ├── beep/                #   - 蜂鸣器驱动
│   │   ├── gpio/                #   - GPIO 驱动
│   │   └── sr04/                #   - 超声波传感器
│   └── modules/                 # 功能模块
│       ├── ota/                 #   - OTA 升级模块
│       ├── web/                 #   - Web 服务器
│       └── heartbeat/           #   - 心跳监控
├── thirdparty/                  # 第三方库目录 (新增)
│   ├── cJSON/                   #   - cJSON JSON 解析库
│   └── mongoose/                #   - Mongoose Web 服务器
├── config/                      # 项目配置
│   ├── sdkconfig                #   - ESP-IDF 配置
│   └── partitions.csv           #   - 分区表
├── docs/                        # 项目文档
│   ├── ARCHITECTURE.md          #   - 架构文档
│   └── PROJECT_STRUCTURE.md     #   - 项目结构 (本文件)
├── include/                     # 公共头文件
│   ├── tasker.h                 #   - 任务调度器 API
│   ├── dtree.h                  #   - 设备树 API
│   ├── event_bus.h              #   - 事件总线 API
│   ├── logger.h                 #   - 日志 API
│   ├── heartbeat.h              #   - 心跳 API
│   ├── ota.h                    #   - OTA API
│   └── web.h                    #   - Web API
├── main/                        # 主程序入口
├── scripts/                     # 工具脚本
├── src/app/                     # 应用代码
├── web/                         # Web 前端源码
├── CMakeLists.txt               # 项目 CMake 配置
└── README.md
```

## 设备树设计

### 设计理念

模仿 Linux 设备树模型，使用 JSON 文件描述硬件配置：
- **配置与代码分离**: 修改引脚只需改 JSON，无需重编译
- **统一管理**: 所有硬件配置集中存放
- **易于维护**: 清晰的层次结构

### JSON 配置示例

```json
{
    "compatible": "i2s-bus",
    "description": "I2S 总线配置",
    
    "bus": {
        "bclk_pin": 6,
        "ws_pin": 5,
        "sample_rate_hz": 16000
    },
    
    "microphone": {
        "compatible": "i2s-microphone",
        "data_in_pin": 7
    }
}
```

### 使用方法

```c
#include "dtree.h"

// 初始化设备树
dtree_init();

// 获取引脚配置
int bclk = DTREE_INT("i2s.bus", "bclk_pin", 6);
int ws = DTREE_INT("i2s.bus", "ws_pin", 5);

// 获取子节点属性
dtree_node_t* mic = dtree_get_node("i2s.microphone");
int din_pin = dtree_get_int(mic, "data_in_pin", 7);
```

## 组件依赖关系

```
main
├── tasker_api ──→ tasker ──→ esp_driver_gptimer, esp_timer, logger
├── dtbs ──→ logger, spiffs, cJSON (thirdparty)
├── wifi ──→ esp_wifi, esp_event, esp_netif, esp_timer, logger
├── led ──→ esp_driver_gpio, gpio
├── ota ──→ app_update, esp_system, logger
├── web ──→ spiffs, esp_http_server, ota, heartbeat, tasker_api, wifi, logger, mongoose (thirdparty)
└── heartbeat ──→ esp_wifi, esp_timer, tasker_api, logger
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
| JSON 文件 | 小写 + 下划线 | `i2s.json`, `system.json` |
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

# OTA 升级
./scripts/ota_update.sh <esp32-ip>
```

## 添加新设备

1. 在 `components/dtbs/config/` 下创建 JSON 文件
2. 描述设备引脚和配置参数
3. 在驱动模块中使用 `dtree` API 读取配置

```c
// 示例：读取新设备配置
dtree_node_t* dev = dtree_get_node("my_device");
int pin = dtree_get_int(dev, "data_pin", -1);
```

## LVGL UI 模块

### 目录结构

```
src/lvgl/
├── lvgl_app.c/h         # LVGL 应用层主文件
├── lv_conf.h            # LVGL 配置文件
├── pages/               # UI 页面
│   ├── page_home.c      #   - 主页
│   ├── page_menu.c      #   - 菜单页
│   ├── page_settings.c  #   - 设置页
│   ├── page_lora.c      #   - LoRa 聊天页
│   ├── page_audio.c     #   - 音频播放页
│   └── page_about.c     #   - 关于页
├── widgets/             # 自定义控件
│   ├── widgets.c/h      #   - 控件实现
├── themes/              # 主题
│   ├── theme_dark.c/h   #   - 暗色主题
├── fonts/               # 字体文件
└── assets/              # 资源文件 (图片、图标)
```

### 使用方法

```c
#include "lvgl_app.h"

// 初始化
lvgl_app_init();

// 切换页面
lvgl_app_switch_page(PAGE_MENU);

// 在主循环中处理
while (1) {
    lvgl_app_handler(10);
}
```

### 添加新页面

1. 在 `pages/` 目录创建 `page_xxx.c`
2. 实现 `void page_xxx_create(lv_obj_t* parent)` 函数
3. 在 `page_id_t` 枚举中添加页面 ID
4. 在 `lvgl_app.c` 的页面函数表中注册

### 自定义控件

常用控件封装在 `widgets/` 目录：
- 状态栏 (`widget_status_bar_*`)
- 消息对话框 (`widget_msgbox_*`)
- 加载动画 (`widget_spinner_*`)

### 主题

暗色主题颜色定义：
- 背景色: `0x1a1a2e`
- 表面色: `0x16213e`
- 主色调: `0x0f3460`
- 强调色: `0x533483`

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
├── themes/                      # 主题
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
