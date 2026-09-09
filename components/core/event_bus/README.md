# Event Bus (事件总线)

## 概述

事件总线是系统核心模块，提供模块间的发布-订阅通信机制，实现模块解耦。

## 特性

- **发布-订阅模式**: 模块间通过事件通信，无需直接依赖
- **线程安全**: 支持多任务并发发布/订阅
- **异步处理**: 事件队列 + 独立处理任务，不阻塞发布者
- **类型安全**: 强类型事件定义，编译期检查
- **资源高效**: 固定大小队列和订阅表，内存占用可预测

## 架构

```
┌─────────────────────────────────────────────────────────┐
│                    Event Bus 核心                         │
├─────────────────────────────────────────────────────────┤
│  Event Types (事件类型枚举)                               │
│  ├── SYSTEM_*      (系统事件)                             │
│  ├── WIFI_*        (WiFi事件)                            │
│  ├── SENSOR_*      (传感器事件)                           │
│  ├── TOUCH_*       (触控事件)                             │
│  ├── LORA_*        (LoRa事件)                            │
│  └── UI_*          (UI事件)                              │
├─────────────────────────────────────────────────────────┤
│  Subscription Table (订阅表)                              │
│  Event Queue (事件队列)                                   │
│  Event Processor Task (处理任务)                          │
└─────────────────────────────────────────────────────────┘
```

## 快速开始

### 1. 初始化

在 `app_main()` 中尽早调用:

```c
#include "event_bus.h"

void app_main(void) {
    // 初始化事件总线 (必须在其他模块前)
    if (event_bus_init() != EVENT_BUS_OK) {
        ESP_LOGE(TAG, "Event bus init failed");
        return;
    }
    
    // 初始化其他模块...
}
```

### 2. 订阅事件

```c
#include "event_bus.h"

// 定义事件处理函数
static int on_wifi_connected(const event_t* event, void* user_data) {
    const event_wifi_connected_t* data = 
        (const event_wifi_connected_t*)event->data;
    
    printf("WiFi connected: %s, RSSI: %d\n", data->ssid, data->rssi);
    
    // 处理事件...
    
    return 0;  // 返回0表示成功
}

// 订阅事件
static event_subscription_t* s_wifi_sub = NULL;

void my_module_init(void) {
    s_wifi_sub = event_bus_subscribe(
        EVENT_WIFI_CONNECTED,   // 事件类型
        on_wifi_connected,      // 处理函数
        NULL                    // 用户数据 (可选)
    );
    
    if (s_wifi_sub == NULL) {
        printf("Subscribe failed\n");
    }
}
```

### 3. 发布事件

```c
#include "event_bus.h"

void wifi_on_connected(const char* ssid, int rssi) {
    // 准备事件数据
    event_wifi_connected_t data = {
        .rssi = rssi,
        .authmode = 0,
        .ip_addr = 0
    };
    strncpy(data.ssid, ssid, sizeof(data.ssid) - 1);
    
    // 发布事件 (使用类型安全宏)
    event_bus_err_t ret = EVENT_BUS_PUBLISH(EVENT_WIFI_CONNECTED, &data);
    
    if (ret != EVENT_BUS_OK) {
        printf("Publish failed: %s\n", event_bus_get_err_name(ret));
    }
}

// 发布无数据事件
void system_on_startup(void) {
    EVENT_BUS_PUBLISH_EMPTY(EVENT_SYSTEM_STARTUP);
}
```

### 4. 取消订阅

```c
void my_module_deinit(void) {
    if (s_wifi_sub != NULL) {
        event_bus_unsubscribe(s_wifi_sub);
        s_wifi_sub = NULL;
    }
}
```

## 事件类型

### 系统事件

| 事件类型 | 说明 | 数据结构 |
|---------|------|---------|
| `EVENT_SYSTEM_STARTUP` | 系统启动完成 | 无 |
| `EVENT_SYSTEM_SHUTDOWN` | 系统即将关闭 | 无 |
| `EVENT_SYSTEM_ERROR` | 系统错误 | `event_system_error_t` |
| `EVENT_SYSTEM_REBOOT` | 系统重启请求 | 无 |
| `EVENT_SYSTEM_WATCHDOG_FEED` | 看门狗喂狗 | 无 |

### WiFi事件

| 事件类型 | 说明 | 数据结构 |
|---------|------|---------|
| `EVENT_WIFI_CONNECTED` | WiFi已连接 | `event_wifi_connected_t` |
| `EVENT_WIFI_DISCONNECTED` | WiFi已断开 | `event_wifi_disconnected_t` |
| `EVENT_WIFI_SCAN_DONE` | 扫描完成 | `event_wifi_scan_done_t` |
| `EVENT_WIFI_SWITCH_START` | 开始切换AP | `event_wifi_switch_t` |
| `EVENT_WIFI_SWITCH_DONE` | 切换完成 | `event_wifi_switch_t` |
| `EVENT_WIFI_SWITCH_FAILED` | 切换失败 | `event_wifi_switch_t` |
| `EVENT_WIFI_GOT_IP` | STA获取IP地址 | 无 |
| `EVENT_WIFI_AP_STARTED` | SoftAP已启动 | 无 |
| `EVENT_WIFI_AP_STOPPED` | SoftAP已停止 | 无 |
| `EVENT_WIFI_MODE_CHANGED` | 网络模式切换完成（net_mgr_switch_mode 触发） | `event_wifi_mode_changed_t` |

### 传感器事件

| 事件类型 | 说明 | 数据结构 |
|---------|------|---------|
| `EVENT_SENSOR_TEMP_HUMIDITY` | 温湿度更新 | `event_sensor_temp_humidity_t` |
| `EVENT_SENSOR_ERROR` | 传感器错误 | 无 |

### 触控事件

| 事件类型 | 说明 | 数据结构 |
|---------|------|---------|
| `EVENT_TOUCH_PRESS` | 触摸按下 | `event_touch_t` |
| `EVENT_TOUCH_RELEASE` | 触摸释放 | `event_touch_t` |
| `EVENT_TOUCH_SWIPE` | 触摸滑动 | `event_touch_t` |
| `EVENT_TOUCH_LONG_PRESS` | 长按 | `event_touch_t` |

### LoRa事件

| 事件类型 | 说明 | 数据结构 |
|---------|------|---------|
| `EVENT_LORA_DATA_RECEIVED` | 接收到数据 | `event_lora_data_received_t` |
| `EVENT_LORA_SEND_COMPLETE` | 发送完成 | `event_lora_send_done_t` |
| `EVENT_LORA_SEND_FAILED` | 发送失败 | `event_lora_send_done_t` |

## API参考

### 初始化与销毁

```c
event_bus_err_t event_bus_init(void);
event_bus_err_t event_bus_deinit(void);
```

### 事件发布

```c
event_bus_err_t event_bus_publish(event_type_t type, const void* data, size_t data_len);

// 便捷宏
EVENT_BUS_PUBLISH(type, data_ptr)
EVENT_BUS_PUBLISH_EMPTY(type)
```

### 事件订阅

```c
event_subscription_t* event_bus_subscribe(event_type_t event_type, 
                                          event_handler_fn_t handler, 
                                          void* user_data);

event_bus_err_t event_bus_unsubscribe(event_subscription_t* subscription);
```

### 状态查询

```c
event_bus_err_t event_bus_get_status(int* queue_size, int* subscriber_count);
event_bus_err_t event_bus_get_stats(uint32_t* events_published, 
                                    uint32_t* events_processed,
                                    uint32_t* events_dropped,
                                    uint32_t* handler_errors);
bool event_bus_is_initialized(void);
```

### 调试辅助

```c
const char* event_bus_get_type_name(event_type_t type);
const char* event_bus_get_err_name(event_bus_err_t err);
void event_bus_print_status(void);
void event_bus_print_subscribers(void);
```

## 配置参数

在 `event_bus_types.h` 中定义:

```c
#define EVENT_BUS_QUEUE_SIZE        32      // 事件队列大小
#define EVENT_BUS_MAX_SUBSCRIBERS   64      // 最大订阅者数量
#define EVENT_BUS_MAX_EVENT_SIZE    256     // 最大事件数据大小 (字节)
#define EVENT_BUS_TASK_STACK_SIZE   4096    // 处理任务栈大小
#define EVENT_BUS_TASK_PRIORITY     5       // 处理任务优先级
#define EVENT_BUS_POLL_INTERVAL_MS  10      // 处理任务轮询间隔 (ms)
```

## 内存使用

- 事件队列: `EVENT_BUS_QUEUE_SIZE * 8` 字节
- 订阅者表: `EVENT_BUS_MAX_SUBSCRIBERS * 20` 字节
- 任务栈: `EVENT_BUS_TASK_STACK_SIZE` 字节
- 事件数据: 动态分配，处理完成后释放

## 注意事项

1. **处理函数阻塞**: 处理函数应尽快返回，复杂操作放入任务队列
2. **事件数据大小**: 建议控制在256字节内，大数据通过指针传递
3. **内存管理**: 事件数据由总线管理，订阅者无需释放
4. **线程安全**: 订阅/取消订阅操作已加锁保护
5. **初始化顺序**: 必须在其他模块使用事件总线前初始化

## 调试

使用串口命令查看事件总线状态:

```c
// 打印事件总线状态
event_bus_print_status();

// 打印所有订阅者
event_bus_print_subscribers();
```

## 版本历史

- v1.0.0 (2026-09-07): 初始版本
  - 基本发布-订阅功能
  - 支持多种事件类型
  - 线程安全设计
  - 统计信息和调试辅助
