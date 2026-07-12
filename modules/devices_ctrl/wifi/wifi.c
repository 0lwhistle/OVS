#include "wifi.h"

// ---------- Wi-Fi 事件回调（AP+STA 共存模式） ----------
void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    // ---- STA 事件 ----
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        printf("Wi-Fi STA started, connecting to AP...\n");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        printf("Wi-Fi STA disconnected, reconnecting...\n");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        printf("Got IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
    }

    // ---- AP 事件 ----
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_AP_START) {
        printf("Wi-Fi AP started, SSID: %s\n", WIFI_AP_SSID);
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event =
            (wifi_event_ap_staconnected_t *)event_data;
        printf("Station connected to AP, MAC: " MACSTR "\n",
               MAC2STR(event->mac));
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event =
            (wifi_event_ap_stadisconnected_t *)event_data;
        printf("Station disconnected from AP, MAC: " MACSTR "\n",
               MAC2STR(event->mac));
    }
}

// ---------- 获取 STA 配置 ----------
void wifi_get_sta_config(wifi_config_t *cfg) {
    wifi_sta_config_t sta = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
    };
    cfg->sta = sta;
}

// ---------- 获取 AP 配置 ----------
void wifi_get_ap_config(wifi_config_t *cfg) {
    wifi_ap_config_t ap = {
        .ssid = WIFI_AP_SSID,
        .password = WIFI_AP_PASSWORD,
        .ssid_len = 0,                     // 自动计算 SSID 长度
        .channel = WIFI_AP_CHANNEL,
        .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        .max_connection = WIFI_AP_MAX_CONN,
        .beacon_interval = 100,
    };
    cfg->ap = ap;
}

// ---------- 初始化 Wi-Fi（AP+STA 共存模式） ----------
void wifi_init(void) {
    // 1. 初始化网络协议栈
    esp_netif_init();
    esp_event_loop_create_default();

    // 2. 创建 STA 和 AP 网络接口
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    // 3. 初始化 Wi-Fi 驱动
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // 4. 注册事件回调（STA + AP 事件）
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               &wifi_event_handler, NULL);

    // 5. 配置为 AP+STA 共存模式
    esp_wifi_set_mode(WIFI_MODE_APSTA);

    // 6. 配置 STA（连接外部路由器）
    wifi_config_t sta_config;
    wifi_get_sta_config(&sta_config);
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);

    // 7. 配置 AP（自身热点）
    wifi_config_t ap_config;
    wifi_get_ap_config(&ap_config);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);

    // 8. 启动 Wi-Fi
    esp_wifi_start();

    printf("Wi-Fi initialized in AP+STA mode\n");
    printf("  STA: connecting to %s\n", WIFI_SSID);
    printf("  AP:  SSID=%s, Password=%s\n", WIFI_AP_SSID, WIFI_AP_PASSWORD);
}
