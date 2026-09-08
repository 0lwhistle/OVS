/**
 * @file wifi.c
 * @brief WiFi 驱动实现（esp_wifi 纯封装层）
 *
 * 策略类逻辑（凭据持久化/配网/热切换回退/模式状态机）在 modules/net_mgr，
 * 本文件只做硬件操作。断线自动重连是驱动级保底行为。
 */

#include "wifi.h"
#include <string.h>
#include <stdio.h>
#include "logger.h"

// 保存 STA 配置（运行时可修改，由 wifi_start_sta 应用）
static wifi_config_t s_sta_config = {0};
static bool s_sta_config_valid = false;

// 缓存 MAC 地址（初始化时获取，后续直接使用）
static char s_mac_str[18] = {0};

// 缓存 STA netif 句柄（默认 STA netif 的 ifkey 是 "WIFI_STA_DEF"，
// 不能用 "STA_DEF" 反查，直接缓存创建时返回的句柄）
static esp_netif_t *s_sta_netif = NULL;

// ---------- AP 扫描相关 ----------
static SemaphoreHandle_t s_scan_sem = NULL;
static wifi_ap_info_t *s_scan_buf = NULL;
static uint16_t s_scan_count = 0;
static uint16_t s_scan_max = 0;

// ---------- 扫描完成回调 ----------
static void wifi_scan_done_handler(void *arg, esp_event_base_t event_base,
                                    int32_t event_id, void *event_data) {
    (void)arg; (void)event_base; (void)event_id; (void)event_data;
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) {
        s_scan_count = 0;
        xSemaphoreGive(s_scan_sem);
        return;
    }

    wifi_ap_record_t *raw = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!raw) {
        s_scan_count = 0;
        xSemaphoreGive(s_scan_sem);
        return;
    }

    esp_wifi_scan_get_ap_records(&ap_count, raw);

    // 按 RSSI 降序排序（冒泡，AP 数量不大）
    for (int i = 0; i < (int)ap_count - 1; i++) {
        for (int j = i + 1; j < (int)ap_count; j++) {
            if (raw[j].rssi > raw[i].rssi) {
                wifi_ap_record_t tmp = raw[i];
                raw[i] = raw[j];
                raw[j] = tmp;
            }
        }
    }

    uint16_t limit = (ap_count < s_scan_max) ? ap_count : s_scan_max;
    for (uint16_t i = 0; i < limit; i++) {
        memcpy(s_scan_buf[i].ssid, raw[i].ssid, sizeof(raw[i].ssid));
        s_scan_buf[i].ssid[32] = '\0';
        s_scan_buf[i].rssi = raw[i].rssi;
        s_scan_buf[i].authmode = raw[i].authmode;
    }
    s_scan_count = limit;
    free(raw);

    xSemaphoreGive(s_scan_sem);
}


// ---------- 常见断开原因码转可读字符串 ----------
static const char *wifi_reason_str(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND:          return "NO_AP_FOUND";
        case WIFI_REASON_AUTH_FAIL:            return "AUTH_FAIL";
        case WIFI_REASON_ASSOC_FAIL:           return "ASSOC_FAIL";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:    return "HANDSHAKE_TIMEOUT";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT(密码错误?)";
        case WIFI_REASON_AUTH_EXPIRE:          return "AUTH_EXPIRE";
        case WIFI_REASON_BEACON_TIMEOUT:       return "BEACON_TIMEOUT";
        case WIFI_REASON_ASSOC_TOOMANY:        return "AP拒绝(连接数满)";
        case WIFI_REASON_ASSOC_LEAVE:          return "AP踢出(可能MAC过滤)";
        default:                               return "OTHER";
    }
}

// ---------- Wi-Fi 事件回调（驱动级保底行为） ----------
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *dis =
            (const wifi_event_sta_disconnected_t *)event_data;
        printf("[WiFi] STA disconnected (reason=%d %s), auto-reconnecting...\n",
               dis->reason, wifi_reason_str(dis->reason));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        printf("[WiFi] Got IP: " IPSTR " (MAC %s)\n",
               IP2STR(&event->ip_info.ip),
               s_mac_str[0] ? s_mac_str : "-");
    }
}

// ---------- 设置 STA 的 SSID 和密码 ----------
int wifi_set_sta_config(const char *ssid, const char *password) {
    if (!ssid || !password) {
        printf("[WiFi] Invalid params: ssid or password is NULL\n");
        return -1;
    }

    size_t ssid_len = strlen(ssid);
    size_t pass_len = strlen(password);
    if (ssid_len == 0 || ssid_len > 32) {
        printf("[WiFi] Invalid SSID length: %zu\n", ssid_len);
        return -1;
    }
    if (pass_len > 64) {
        printf("[WiFi] Invalid password length: %zu\n", pass_len);
        return -1;
    }

    memset(&s_sta_config, 0, sizeof(s_sta_config));
    memcpy(s_sta_config.sta.ssid, ssid, ssid_len);
    memcpy(s_sta_config.sta.password, password, pass_len);
    s_sta_config_valid = true;
    return 0;
}

// ---------- 初始化 Wi-Fi（仅准备协议栈，不启动） ----------
void wifi_init(void) {
    // 1. 初始化网络协议栈与事件循环
    esp_netif_init();
    esp_event_loop_create_default();

    // 2. 创建 STA 网络接口并缓存 MAC 地址
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif) {
        s_sta_netif = sta_netif;
        uint8_t mac[6] = {0};
        if (esp_netif_get_mac(sta_netif, mac) == ESP_OK) {
            snprintf(s_mac_str, sizeof(s_mac_str), MACSTR, MAC2STR(mac));
            printf("[WiFi] MAC address: %s\n", s_mac_str);
        }
    }

    // 3. 创建 AP 网络接口（SoftAP 模式按需启用）
    if (!esp_netif_create_default_wifi_ap()) {
        printf("[WiFi] Failed to create AP netif\n");
    }

    // 4. 初始化 Wi-Fi 驱动
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        printf("[WiFi] esp_wifi_init failed\n");
        return;
    }

    // 5. 注册事件回调（ANY_ID 覆盖 STA/AP 全部事件）
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               &wifi_event_handler, NULL);

    // 6. 默认 STA 模式；不设置凭据、不启动，
    //    由 net_mgr 调用 wifi_start_sta()/wifi_start_ap() 控制
    esp_wifi_set_mode(WIFI_MODE_STA);

    printf("[WiFi] stack initialized (idle, mode=STA pending)\n");
}

// ---------- 应用 STA 配置并启动 ----------
int wifi_start_sta(void) {
    if (!s_sta_config_valid) {
        printf("[WiFi] wifi_start_sta: no STA config set\n");
        return -1;
    }

    esp_wifi_disconnect();
    esp_wifi_stop();

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_STA, &s_sta_config) != ESP_OK ||
        esp_wifi_start() != ESP_OK) {
        printf("[WiFi] wifi_start_sta failed\n");
        return -1;
    }

    // STA_START 事件回调自动 esp_wifi_connect()
    printf("[WiFi] STA starting, connecting to %s\n",
           (char *)s_sta_config.sta.ssid);
    return 0;
}

// ---------- 启动 SoftAP ----------
int wifi_start_ap(const char *ssid, const char *password,
                  uint8_t channel, uint8_t max_conn) {
    if (!ssid || ssid[0] == '\0' || strlen(ssid) > 32) {
        printf("[WiFi] wifi_start_ap: invalid ssid\n");
        return -1;
    }
    size_t pass_len = password ? strlen(password) : 0;
    if (pass_len > 0 && pass_len < 8) {
        printf("[WiFi] wifi_start_ap: password too short (min 8)\n");
        return -1;
    }

    esp_wifi_disconnect();
    esp_wifi_stop();

    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) {
        printf("[WiFi] set AP mode failed\n");
        return -1;
    }

    wifi_config_t ap_config = {0};
    memcpy(ap_config.ap.ssid, ssid, strlen(ssid));
    ap_config.ap.ssid_len = (uint8_t)strlen(ssid);
    if (pass_len > 0) {
        memcpy(ap_config.ap.password, password, pass_len);
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    ap_config.ap.channel = channel ? channel : 6;
    ap_config.ap.max_connection = max_conn ? max_conn : 4;

    if (esp_wifi_set_config(WIFI_IF_AP, &ap_config) != ESP_OK ||
        esp_wifi_start() != ESP_OK) {
        printf("[WiFi] wifi_start_ap failed\n");
        return -1;
    }

    printf("[WiFi] AP started: SSID=%s, ch=%d, %s, max_conn=%d\n",
           ssid, ap_config.ap.channel,
           pass_len > 0 ? "WPA2" : "OPEN", ap_config.ap.max_connection);
    return 0;
}

// ---------- 停止射频 ----------
int wifi_stop(void) {
    if (esp_wifi_stop() != ESP_OK) {
        return -1;
    }
    printf("[WiFi] radio stopped\n");
    return 0;
}

// ---------- 获取当前模式 ----------
wifi_mode_t wifi_get_mode(void) {
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK) {
        return WIFI_MODE_NULL;
    }
    return mode;
}

// ---------- 扫描附近 Wi-Fi 热点 ----------
int wifi_scan_aps(wifi_ap_info_t *aps, int max_ap) {
    if (!aps || max_ap <= 0) return 0;

    s_scan_sem = xSemaphoreCreateBinary();
    if (!s_scan_sem) return 0;

    s_scan_buf = aps;
    s_scan_max = (uint16_t)max_ap;
    s_scan_count = 0;

    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                               &wifi_scan_done_handler, NULL);

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 100, .max = 300 } },
    };

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
    if (err != ESP_OK) {
        LOGE("[WiFi]", "esp_wifi_scan_start failed: %s", esp_err_to_name(err));
        esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                     &wifi_scan_done_handler);
        vSemaphoreDelete(s_scan_sem);
        s_scan_sem = NULL;
        return 0;
    }

    // 等待扫描完成（最多 5 秒）
    if (xSemaphoreTake(s_scan_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        esp_wifi_scan_stop();
        s_scan_count = 0;
    }

    esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                 &wifi_scan_done_handler);
    vSemaphoreDelete(s_scan_sem);
    s_scan_sem = NULL;

    return (int)s_scan_count;
}

// ---------- 获取 STA 信号强度 ----------
int wifi_get_rssi(void) {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return 0;   // 未连接/未启动
    }
    return ap.rssi;
}

// ---------- 获取 STA 网络信息内部实现 ----------
static int wifi_fetch_ip_info(esp_netif_ip_info_t *ip_info) {
    if (!s_sta_netif) {
        printf("[WiFi] STA netif not created\n");
        return -1;
    }
    if (esp_netif_get_ip_info(s_sta_netif, ip_info) != ESP_OK) {
        printf("[WiFi] Failed to get IP info\n");
        return -1;
    }
    return 0;
}

// ---------- 获取 Wi-Fi 网络信息 ----------
int wifi_get_net_info(wifi_net_info_t *info) {
    if (!info) {
        return -1;
    }

    memset(info, 0, sizeof(wifi_net_info_t));

    // 1. 获取 IP、子网掩码、网关
    esp_netif_ip_info_t ip_info;
    if (wifi_fetch_ip_info(&ip_info) == 0) {
        snprintf(info->ip, sizeof(info->ip), IPSTR, IP2STR(&ip_info.ip));
        snprintf(info->netmask, sizeof(info->netmask), IPSTR, IP2STR(&ip_info.netmask));
        snprintf(info->gw, sizeof(info->gw), IPSTR, IP2STR(&ip_info.gw));
    }

    // 2. 使用缓存的 MAC 地址
    if (s_mac_str[0] != '\0') {
        memcpy(info->mac, s_mac_str, sizeof(info->mac));
    } else if (s_sta_netif) {
        // 兜底：直接查询
        uint8_t mac[6] = {0};
        if (esp_netif_get_mac(s_sta_netif, mac) == ESP_OK) {
            snprintf(info->mac, sizeof(info->mac), MACSTR, MAC2STR(mac));
        }
    }

    return 0;
}

// ---------- 获取 STA IP 地址 ----------
int wifi_get_ip(char *buf, int len) {
    if (!buf || len <= 0) return -1;
    esp_netif_ip_info_t ip_info;
    if (wifi_fetch_ip_info(&ip_info) != 0) return -1;
    snprintf(buf, len, IPSTR, IP2STR(&ip_info.ip));
    return 0;
}

// ---------- 获取 STA 网关 ----------
int wifi_get_gateway(char *buf, int len) {
    if (!buf || len <= 0) return -1;
    esp_netif_ip_info_t ip_info;
    if (wifi_fetch_ip_info(&ip_info) != 0) return -1;
    snprintf(buf, len, IPSTR, IP2STR(&ip_info.gw));
    return 0;
}

// ---------- 获取 STA 子网掩码 ----------
int wifi_get_netmask(char *buf, int len) {
    if (!buf || len <= 0) return -1;
    esp_netif_ip_info_t ip_info;
    if (wifi_fetch_ip_info(&ip_info) != 0) return -1;
    snprintf(buf, len, IPSTR, IP2STR(&ip_info.netmask));
    return 0;
}
