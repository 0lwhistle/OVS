---
name: esp32s3-smart-assistant
description: ESP32-S3桌面智能助手开发技能。当用户进行ESP32-S3智能助手项目开发时使用，包括：代码模块化开发、功能模块实现、硬件驱动开发、LoRa通讯、音频处理、LVGL界面开发、Web上位机开发等。强制要求使用日志系统记录开发进度和问题。强调代码可移植性设计,边界情况考虑和错误处理、模块化设计、代码库复用（已有的功能库直接使用，不重复造轮子）。要求行为尽量节省token，在阅读串口和脚本、编译等等时，只阅读末尾最关键部分就可以，同时这也要求编写脚本时做好错误处理，每次阅读脚本末尾时可以获取关键信息。
---

# ESP32-S3 桌面智能助手开发技能

## 项目概述

**项目名称**: OVS (Open Voice Assistant)  
**硬件平台**: ESP32-S3  
**开发框架**: ESP-IDF v6.0.1  
**项目路径**: `/home/olwhistle/dockerNow/esp32/programs/ovs`

---

## 一、项目目录结构

```
ovs/
├── CMakeLists.txt               # 项目CMake配置
├── sdkconfig                    # ESP-IDF配置
│
├── main/                        # main 组件垫片（IDF 要求组件名 main，
│                                #   仅 CMakeLists，注册 src/app 源文件）
│
├── components/                  # 组件目录
│   ├── core/                    # 核心服务层
│   │   ├── event_bus/           #   事件总线 (发布订阅)
│   │   ├── tasker/              #   任务调度器 (三级优先级)
│   │   ├── logger/              #   日志系统 (彩色分级)
│   │   └── ovs_vfs/             #   VFS 虚拟文件系统
│   │
│   ├── drivers/                 # 硬件驱动层（纯硬件操作，无策略）
│   │   ├── spi_drv/             #   SPI驱动
│   │   ├── uart_drv/            #   UART驱动
│   │   ├── i2s_drv/             #   I2S驱动
│   │   ├── i2c_drv/             #   I2C驱动
│   │   ├── gpio/                #   GPIO驱动
│   │   ├── led/                 #   LED驱动
│   │   └── wifi/                #   WiFi驱动（esp_wifi封装，启停/扫描/网络信息）
│   │
│   ├── modules/                 # 功能模块层（业务策略）
│   │   ├── st7789/              #   ST7789显示屏
│   │   ├── w25q128/             #   W25Q128 Flash
│   │   ├── internal_flash/      #   内部Flash VFS适配
│   │   ├── lora/                #   LoRa无线模块
│   │   ├── audio_module/        #   音频模块
│   │   ├── aht30/               #   AHT30传感器
│   │   ├── cst816s/             #   CST816S触摸屏
│   │   ├── holder/              #   模块注册表管理器
│   │   ├── heartbeat/           #   心跳监控
│   │   ├── net_mgr/             #   网络管理器（STA/AP状态机、凭据NVS/设备树、
│   │   │                        #     配网provider接口、热切换回退、mDNS ovs.local）
│   │   ├── ota/                 #   OTA升级（流式直写、SHA256、回滚保护）
│   │   └── web/                 #   Web服务器（路由注册表、流式OTA上传、WS推送）
│   │
│   └── dtbs/                    # 设备树层
│       ├── dtree.h/.c           #   解析器（公共API在 include/dtree.h）
│       └── config/
│           └── ovs.dtb.json     #   单棵树设备树配置（SPIFFS镜像源）
│
├── src/                         # 源代码
│   ├── app/                     # 应用代码（main.c 入口、应用级测试；
│   │                            #   OTA保护区注释在此文件头部与app_main内）
│   └── lvgl/                    # LVGL UI模块 (六层架构)
│       ├── ui/                  #   控件层
│       ├── widgets/             #   窗口层
│       ├── pages/               #   页面层
│       ├── navigator/           #   导航层
│       ├── presenters/          #   展示器层
│       └── bridge/              #   桥接层
│
├── include/                     # 公共头文件
├── scripts/                     # 构建脚本
├── docs/                        # 文档
└── web/                         # Web前端
```

---

## 二、核心架构

### 2.1 Event Bus - 事件总线

**作用**: 模块间发布-订阅通信，实现完全解耦

```c
#include "event_bus.h"

// 发布事件
EVENT_BUS_PUBLISH(EVENT_TYPE, &data);      // 带数据
EVENT_BUS_PUBLISH_EMPTY(EVENT_TYPE);       // 无数据

// 订阅事件
event_subscription_t* sub = event_bus_subscribe(
    EVENT_WIFI_CONNECTED,   // 事件类型
    on_wifi_connected,      // 处理函数
    NULL                    // 用户数据
);

// 取消订阅
event_bus_unsubscribe(sub);
```

**事件类型编码**: `(MODULE_ID << 16) | EVENT_ID`

### 2.2 Tasker - 任务调度器

**作用**: 三级优先级队列，周期性任务管理

| 级别 | 超时 | 栈大小 | 适用场景 |
|------|------|--------|----------|
| Little | 50ms | 3KB | GPIO控制、状态查询 |
| Middle | 1000ms | 4KB | 传感器读取、UI更新 |
| Lots | 10000ms | 8KB | 文件操作、OTA升级 |

```c
#include "tasker.h"

// 创建周期任务
struct task_node node;
tasker_task_init_mi(&node, 1000, -1, "task_name", task_fn, ctx);
tasker_enqueue(&node);
```

### 2.3 Net Manager - 网络管理器 (modules/net_mgr)

**作用**: STA/AP 模式状态机、凭据持久化、配网通道、热切换回退

```c
#include "net_mgr.h"

net_mgr_init(NULL);              // NULL = NVS/设备树默认配置
net_mgr_start(NET_MODE_STA);     // STA/AP 互斥切换
net_status_t st;
net_mgr_get_status(&st);         // mode/state/ssid/ip/rssi/switching

// 配网通道（web 已注册，蓝牙模块实现后注册即接入）
net_provision_register(&provider);
net_provision_submit(ssid, pass);  // 凭据统一入口: 持久化+切换+30s回退
```

**凭据优先级**: 显式config > 设备树变更(哈希变化→覆盖NVS) > NVS(用户配网，跨OTA幸存)
**覆盖规则**: 烧录含新 wifi.sta 的设备树后，下次启动自动覆盖 NVS；设备树未变则用户配网配置持续生效
**热切换**: 30s 超时自动回退 RAM 配置 + NVS

### 2.4 OTA - 固件升级 (modules/ota)

**流式直写**: `ota_begin(expected) → ota_write(chunk) → ota_end()`，无整包缓冲；
回滚保护: 启动15s后自动确认有效，崩溃则bootloader回退旧槽。
开发期推送: `./scripts/ota_push.sh`（详见 docs/ota_guide.md）。

### 2.5 Device Tree - 设备树

**作用**: JSON配置硬件参数，代码与配置分离

```c
#include "dtree.h"

// 读取配置
int32_t pin;
DTREE_INT("spi.lcd_display", "cs_pin", &pin);
```

### 2.6 LVGL - 六层UI架构

```
pages (页面层) → widgets (窗口层) → ui (控件层)
                    ↑
navigator (导航层) ← presenters (展示器层) ← bridge (桥接层)
```

---

## 三、重点文档位置

| 文档 | 路径 | 说明 |
|------|------|------|
| **架构设计** | `docs/ARCHITECTURE.md` | 系统架构详解 (必读) |
| 项目结构 | `docs/PROJECT_STRUCTURE.md` | 目录结构权威说明 |
| **开发日志** | `docs/development_log.md` | 开发进度记录 (每次开发必更新) |
| **OTA指南** | `docs/ota_guide.md` | OTA 使用文档（免串口迭代流程） |
| **网络/OTA日志** | `docs/development_log_net_ota.md` | OTA+网络线独立日志 |
| 交接摘要 | `docs/handoff_summary.md` | 跨会话上下文交接 |
| 设备树配置 | `components/dtbs/config/*.json` | JSON硬件配置 |
| LVGL架构 | `src/lvgl/README.md` | LVGL六层架构说明 |
| 外设驱动 | `docs/peripheral_drivers_summary.md` | 驱动实现总结 |

> 注：早期(v1)参考资料 `references/`（project_structure.md、coding_standards.md）已删除，
> 项目结构与编码规范以本文档和 `docs/` 下的文档为准。
> 另：根目录已无 ARCHITECTURE.md，架构文档位于 `docs/ARCHITECTURE.md`。
> 设备树配置自 2026-09-08 起为单棵树 `components/dtbs/config/ovs.dtb.json`（原分总线 JSON 已删除）。

---

## 四、启动流程（按需读取）

1. **必读**: `docs/ARCHITECTURE.md` 了解系统架构
2. **必做**: 读取 `docs/development_log.md` 末尾，恢复开发进度和待解决问题
3. **仅当任务涉及硬件时**: 读取对应的设备树JSON配置
4. **仅当任务涉及LVGL UI时**: 读取 `src/lvgl/README.md`
5. **不要一次性读取所有文档**

---

## 五、硬件配置速查

### I2S 总线（麦克风 + 功放）
- GPIO6: BCLK, GPIO5: WS
- GPIO7: DIN(麦克风), GPIO15: DOUT(功放)

### I2C 总线（触控 + AHT30）
- GPIO16: SDA, GPIO17: SCL
- GPIO18: INT(触控), GPIO39: RST(触控)

### SPI 总线（显示屏 + W25Q128）
- GPIO42: SCLK, GPIO41: MISO, GPIO40: MOSI
- GPIO48: CS_LCD, GPIO47: DC_LCD, GPIO21: RST_LCD, GPIO38: BL_LCD
- GPIO13: CS_W25Q128

### LoRa 模块（UART）
- GPIO9: TXD, GPIO10: RXD
- GPIO8: M0, GPIO3: M1, GPIO46: AUX

### WiFi（配置在设备树 ovs.dtb.json 的 wifi 节点）
- STA 凭据: wifi.sta.ssid / wifi.sta.password（出厂默认，首次启动播种NVS；
  用户配网后 NVS 优先）
- AP 参数: wifi.ap.{ssid_prefix,password,channel,max_connection}
- 主机名: ovs.local（mDNS）

### 调试串口
- GPIO43: TX, GPIO44: RX

### LED
- GPIO4: 状态指示灯

---

## 六、代码规范

### 6.1 命名规范

| 类型 | 规范 | 示例 |
|------|------|------|
| 函数 | `module_action()` | `lora_send_data()` |
| 变量 | `s_module_variable` (静态) | `s_spi_handle` |
| 全局 | `g_module_variable` | `g_initialized` |
| 类型 | `module_type_t` | `lora_msg_t` |
| 宏 | `MODULE_CONSTANT` | `LORA_MAX_SIZE` |
| 枚举 | `MODULE_ENUM_VALUE` | `LORA_ERR_TIMEOUT` |

### 6.2 错误处理

```c
// 使用 goto cleanup 模式
my_err_t my_function(void) {
    my_err_t ret = MY_OK;
    resource_t* res = NULL;
    
    res = allocate_resource();
    if (!res) {
        LOGE(TAG, "Allocate failed");
        ret = MY_ERR_NO_MEMORY;
        goto cleanup;
    }
    
    // ... 业务逻辑 ...
    
cleanup:
    if (res) {
        free_resource(res);
    }
    return ret;
}
```

### 6.3 日志规范

```c
#include "logger.h"

static const char* TAG = "[MY_MODULE]";

LOGI(TAG, "Info message: %d", value);      // 绿色
LOGW(TAG, "Warning message");               // 黄色
LOGE(TAG, "Error message: %s", err_str);   // 红色
LOGD(TAG, "Debug message");                 // 灰色/蓝色
```

---

## 七、可移植性编程要求 ⭐

### 7.1 核心原则

**目标**: 代码应能方便地移植到不同平台（STM32、RK3399、其他ESP系列等）

### 7.2 硬件抽象层 (HAL) 设计

```c
// ❌ 错误：直接使用ESP-IDF API
#include "driver/gpio.h"
gpio_set_level(GPIO_NUM_4, 1);

// ✅ 正确：通过驱动层抽象
#include "gpio_drv.h"
gpio_drv_set_level(s_gpio_handle, PIN_LED, 1);
```

### 7.3 平台相关代码隔离

```c
// my_module_platform.h - 平台抽象接口
#ifndef MY_MODULE_PLATFORM_H
#define MY_MODULE_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

// 平台无关的接口定义
typedef struct {
    int pin;
    int mode;
} gpio_config_t;

// 平台相关的实现（在不同平台有不同的.c文件）
int platform_gpio_init(const gpio_config_t* config);
int platform_gpio_set(int pin, int level);
int platform_gpio_get(int pin);

#endif
```

### 7.4 依赖注入模式

```c
// ❌ 错误：硬编码依赖
void my_module_init(void) {
    spi_drv_init(&config, &handle);  // 直接依赖SPI驱动
}

// ✅ 正确：依赖注入
void my_module_init(const my_module_deps_t* deps) {
    s_spi = deps->spi;  // 通过依赖注入
}
```

### 7.5 条件编译最小化

```c
// ❌ 错误：到处使用条件编译
#ifdef ESP_PLATFORM
    // ESP32 specific code
#elif defined(STM32)
    // STM32 specific code
#endif

// ✅ 正确：集中在平台层
// platform_esp32.c
int platform_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
    return 0;
}

// platform_stm32.c
int platform_delay_ms(uint32_t ms) {
    HAL_Delay(ms);
    return 0;
}
```

### 7.6 标准类型优先

```c
// ❌ 错误：使用平台特定类型
#include "esp_types.h"
esp_err_t ret;

// ✅ 正确：使用标准类型
#include <stdint.h>
#include <stdbool.h>
int ret;  // 或自定义错误码枚举
```

### 7.7 内存管理抽象

```c
// ❌ 错误：直接使用FreeRTOS API
void* ptr = pvPortMalloc(size);
vPortFree(ptr);

// ✅ 正确：使用标准库或抽象层
#include <stdlib.h>
void* ptr = malloc(size);
free(ptr);
```

### 7.8 时钟和延时抽象

```c
// platform.h
uint32_t platform_get_tick_ms(void);
void platform_delay_ms(uint32_t ms);
uint64_t platform_get_time_us(void);
```

### 7.9 文件系统抽象

```c
// file_ops.h
typedef struct file_ops {
    int (*open)(const char* path, int flags);
    int (*read)(int fd, void* buf, size_t count);
    int (*write)(int fd, const void* buf, size_t count);
    int (*close)(int fd);
} file_ops_t;
```

### 7.10 可移植性检查清单

在编写代码时，检查以下项目：

- [ ] 是否直接使用了平台特定的头文件？
- [ ] 是否直接调用了平台特定的API？
- [ ] 是否使用了平台特定的数据类型？
- [ ] 是否有条件编译散布在业务代码中？
- [ ] 是否有硬编码的引脚号/地址？
- [ ] 是否有硬编码的时钟频率？
- [ ] 是否有平台特定的内存分配？

---

## 八、构建命令

```bash
# 设置ESP-IDF环境
source /home/olwhistle/dockerNow/esp32/ESP-IDF/esp-idf-v6.0.1/export.sh

# 完整构建
idf.py build

# 或使用构建脚本
./scripts/mybuild.sh

# 清理后重新构建
idf.py fullclean && idf.py build

# 烧录
idf.py -p /dev/ttyUSB0 flash

# 监控
idf.py -p /dev/ttyUSB0 monitor

# 烧录并监控
idf.py -p /dev/ttyUSB0 flash monitor

# OTA 推送（日常免串口迭代）
./scripts/ota_push.sh [ovs.local|IP]
```

---

## 九、开发流程

### 9.1 添加新模块

1. 在 `components/modules/` 下创建目录
2. 创建头文件 `my_module.h` (公共API)
3. 创建实现文件 `my_module.c`
4. 创建 `CMakeLists.txt` (指定依赖)
5. 在项目 `CMakeLists.txt` 注册
6. 在 `src/app/main.c` 中初始化

### 9.2 添加新事件

1. 在 `event_bus_types.h` 添加事件类型
2. 在 `event_bus_types.h` 添加事件数据结构
3. 在模块中使用 `EVENT_BUS_PUBLISH` 发布
4. 在需要的模块中使用 `event_bus_subscribe` 订阅

### 9.3 添加新任务

1. 实现任务回调函数 `enum task_t task_fn(void* ctx)`
2. 使用 `tasker_task_init_*` 初始化任务节点
3. 使用 `tasker_enqueue` 提交到调度器

### 9.4 添加设备树配置

1. 在 `components/dtbs/config/` 添加JSON文件
2. 在驱动中使用 `DTREE_*` 宏读取配置

### 9.5 开发日志（强制）

每次开发结束时，在 `docs/development_log.md` 追加条目（新条目加在文件顶部），沿用现有条目格式，包含：任务目标、完成内容、待解决问题、解决方案、下一步计划、代码变更。

---

## 十、重要原则

### 10.1 架构原则

- **分层设计**: 应用层 → 模块层 → 核心服务层 → 驱动层 → 设备树层
- **事件驱动**: 通过Event Bus实现模块间解耦
- **配置分离**: 硬件参数通过设备树配置，不硬编码
- **任务调度**: 周期性任务通过Tasker管理

### 10.2 编码原则

- **可移植性**: 代码应能方便移植到不同平台
- **模块化**: 每个模块职责单一，接口清晰
- **错误处理**: 所有API返回错误码，使用goto cleanup模式
- **日志记录**: 使用Logger记录关键操作和错误

### 10.3 LVGL原则

- **分层解耦**: UI与业务逻辑完全分离
- **回调机制**: UI层通过回调通知Presenter
- **桥接模式**: 通过Bridge屏蔽后端实现

### 10.4 调试原则

- **按需读取**: 只读取与当前任务相关的文档
- **不确定问用户**: 不要盲目读所有文档
- **日志追踪**: 使用LOGI/LOGW/LOGE/LOGD记录关键信息

---

## 十一、常见问题

### Q1: 事件没有被接收

**排查**:
```c
event_bus_print_status();        // 检查事件总线状态
event_bus_print_subscribers();   // 检查订阅者
```

### Q2: 任务未执行

**排查**:
```c
if (tasker_is_full()) {
    LOGE(TAG, "Task queue is full");
}
```

### Q3: 设备树读取失败

**排查**:
```c
if (!dtree_is_initialized()) {
    LOGE(TAG, "Device tree not initialized");
}
if (!dtree_has_node("my_device.sensor")) {
    LOGE(TAG, "Node not found");
}
```

---

## 十二、参考资料

- **架构文档**: `docs/ARCHITECTURE.md`
- **ESP-IDF文档**: https://docs.espressif.com/projects/esp-idf/
- **LVGL文档**: https://docs.lvgl.io/
- **FreeRTOS文档**: https://www.freertos.org/

---

**技能版本**: v2.2 (2026-09-08 网络架构更新: net_mgr/OTA流式/src/app/设备树WiFi)  
**最后更新**: 2026-09-08  
**维护团队**: OVS Team
