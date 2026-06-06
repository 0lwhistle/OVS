#ifndef WIFI_H
#define WIFI_H

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---------- Wi-Fi STA 配置 ----------
#define WIFI_SSID       "816"
#define WIFI_PASSWORD   "716717nb"

// ---------- Wi-Fi AP 配置 ----------
#define WIFI_AP_SSID        "ESP32_AP"
#define WIFI_AP_PASSWORD    "12345678"
#define WIFI_AP_CHANNEL     1
#define WIFI_AP_MAX_CONN    4

// ---------- Wi-Fi 事件回调（IDF 方式） ----------
void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

// 获取 STA 网络配置
void wifi_get_sta_config(wifi_config_t *cfg);

// 获取 AP 网络配置
void wifi_get_ap_config(wifi_config_t *cfg);

// ---------- 初始化 Wi-Fi（AP+STA 共存模式） ----------
void wifi_init(void);

#endif // WIFI_H
