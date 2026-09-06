# Bridge 桥接层

## 职责

为 presenters 提供统一的数据接口，屏蔽后端实现细节。

## 设计原则

- UI 层不直接调用后端/硬件
- 通过 bridge 获取数据和执行操作
- 便于单元测试和模拟
- 后端变化只影响 bridge，不影响 UI

## 桥接模块

| 模块 | 文件 | 对接后端 |
|------|------|----------|
| WiFi | bridge_wifi.c | wifi driver, esp_wifi |
| LoRa | bridge_lora.c | lora_manager |
| 音频 | bridge_audio.c | audio_manager |
| 系统 | bridge_system.c | esp_system, heartbeat |
| 存储 | bridge_storage.c | spiffs, flash_manager |

## 接口规范

```c
/* WiFi 桥接 */
typedef struct {
    char ssid[33];
    int rssi;
    bool connected;
} bridge_wifi_status_t;

void bridge_wifi_get_status(bridge_wifi_status_t* status);
void bridge_wifi_connect(const char* ssid, const char* password);
void bridge_wifi_scan(void (*result_cb)(bridge_wifi_ap_t* aps, int count));

/* LoRa 桥接 */
void bridge_lora_send(const uint8_t* data, size_t len);
void bridge_lora_set_recv_cb(void (*recv_cb)(const uint8_t* data, size_t len));

/* 系统桥接 */
void bridge_system_get_info(bridge_system_info_t* info);
uint8_t bridge_system_get_battery(void);
```

## 回调传递

bridge 的异步结果通过回调函数返回，后续可改为 event_bus 传递。
