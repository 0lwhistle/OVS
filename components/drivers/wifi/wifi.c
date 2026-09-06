#include "wifi.h"
#include <string.h>
#include "logger.h"

// 保存用户设置的 STA 配置（运行时可修改）
static wifi_config_t s_sta_config = {0};
static bool s_sta_config_valid = false;

// 缓存 MAC 地址（初始化时获取，后续直接使用）
static char s_mac_str[18] = {0};

// ---------- AP 扫描相关 ----------
static SemaphoreHandle_t s_scan_sem = NULL;
static wifi_ap_info_t *s_scan_buf = NULL;
static uint16_t s_scan_count = 0;
static uint16_t s_scan_max = 0;

// ---------- 热切换回退相关 ----------
static wifi_config_t s_fallback_config = {0};
static bool s_fallback_valid = false;
static esp_timer_handle_t s_rollback_timer = NULL;
static wifi_state_t s_wifi_state = WIFI_STATE_IDLE;
static char s_current_ssid[33] = {0};
static char s_target_ssid[33] = {0};
static int64_t s_switch_start_us = 0;


// ---------- 扫描完成回调 ----------
static void wifi_scan_done_handler(void *arg, esp_event_base_t event_base,
                                    int32_t event_id, void *event_data) {
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

// ---------- Wi-Fi 事件回调 ----------
void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
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

        printf("========== WiFi Network Info ==========\n");
        printf("  IP      : " IPSTR "\n", IP2STR(&event->ip_info.ip));
        printf("  Netmask : " IPSTR "\n", IP2STR(&event->ip_info.netmask));
        printf("  Gateway : " IPSTR "\n", IP2STR(&event->ip_info.gw));
        if (s_mac_str[0] != '\0') {
            printf("  MAC     : %s\n", s_mac_str);
        }
        printf("======================================\n");

        // 热切换成功：取消回退定时器，清除回退配置
        if (s_wifi_state == WIFI_STATE_SWITCHING) {
            if (s_rollback_timer) {
                esp_timer_stop(s_rollback_timer);
                esp_timer_delete(s_rollback_timer);
                s_rollback_timer = NULL;
            }
            s_fallback_valid = false;
            s_wifi_state = WIFI_STATE_CONNECTED;
            memcpy(s_current_ssid, s_target_ssid, sizeof(s_current_ssid));
            LOGI("[WiFi]", "Hot-switch SUCCESS, now connected to %s", s_current_ssid);
        } else {
            // 正常连接：更新当前 SSID
            wifi_config_t cur;
            if (esp_wifi_get_config(WIFI_IF_STA, &cur) == ESP_OK) {
                memcpy(s_current_ssid, cur.sta.ssid, sizeof(s_current_ssid));
                s_wifi_state = WIFI_STATE_CONNECTED;
            }
        }
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

    printf("[WiFi] STA config updated: SSID=%s, Password=%s\n", ssid, password);
    return 0;
}

// ---------- 完全重启 Wi-Fi STA ----------
void wifi_restart_sta(void) {
    printf("\n========== WiFi STA Restart ==========\n");

    // 1. 断开当前连接
    esp_wifi_disconnect();

    // 2. 停止 Wi-Fi 驱动
    esp_wifi_stop();

    // 3. 注销事件处理器
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                 &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                 &wifi_event_handler);

    // 4. 释放 netif 和 event loop
    esp_netif_deinit();
    esp_event_loop_delete_default();

    // 5. 重新初始化网络协议栈
    esp_netif_init();
    esp_event_loop_create_default();

    // 6. 重新创建 STA 网络接口并缓存 MAC
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (!sta_netif) {
        printf("[WiFi] Failed to create STA netif during restart\n");
        return;
    }
    // 缓存 MAC 地址
    uint8_t mac[6] = {0};
    if (esp_netif_get_mac(sta_netif, mac) == ESP_OK) {
        snprintf(s_mac_str, sizeof(s_mac_str), MACSTR, MAC2STR(mac));
    }

    // 7. 重新初始化 Wi-Fi 驱动
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        printf("[WiFi] esp_wifi_init failed during restart: %s\n",
               esp_err_to_name(ret));
        return;
    }

    // 8. 重新注册事件回调
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               &wifi_event_handler, NULL);

    // 9. 设置为 STA 模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // 10. 应用 STA 配置（优先使用用户设置，否则用默认值）
    wifi_config_t config;
    if (s_sta_config_valid) {
        memcpy(&config, &s_sta_config, sizeof(config));
    } else {
        memset(&config, 0, sizeof(config));
        memcpy(config.sta.ssid, WIFI_SSID, strlen(WIFI_SSID));
        memcpy(config.sta.password, WIFI_PASSWORD, strlen(WIFI_PASSWORD));
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));

    // 11. 启动 Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_start());

    printf("[WiFi] STA restart complete, connecting to %s\n",
           (char *)config.sta.ssid);
    printf("========== WiFi STA Restart Done ==========\n");
}

// ---------- 获取 Wi-Fi 网络信息 ----------
int wifi_get_net_info(wifi_net_info_t *info) {
    if (!info) {
        return -1;
    }

    memset(info, 0, sizeof(wifi_net_info_t));

    // 1. 获取 IP、子网掩码、网关
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("STA_DEF");
    if (!netif) {
        printf("[WiFi] Failed to get STA netif handle\n");
        return -1;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(netif, &ip_info);
    if (ret == ESP_OK) {
        snprintf(info->ip, sizeof(info->ip), IPSTR, IP2STR(&ip_info.ip));
        snprintf(info->netmask, sizeof(info->netmask), IPSTR, IP2STR(&ip_info.netmask));
        snprintf(info->gw, sizeof(info->gw), IPSTR, IP2STR(&ip_info.gw));
    } else {
        printf("[WiFi] Failed to get IP info: %s\n", esp_err_to_name(ret));
    }

    // 2. 使用缓存的 MAC 地址
    if (s_mac_str[0] != '\0') {
        memcpy(info->mac, s_mac_str, sizeof(info->mac));
    } else {
        // 兜底：直接查询
        uint8_t mac[6] = {0};
        ret = esp_netif_get_mac(netif, mac);
        if (ret == ESP_OK) {
            snprintf(info->mac, sizeof(info->mac), MACSTR, MAC2STR(mac));
        }
    }

    return 0;
}


// ---------- 回退定时器回调 ----------
static void wifi_rollback_timer_cb(void *arg) {
    LOGE("[WiFi]", "Switch timeout (30s), rolling back to previous AP");

    if (s_fallback_valid) {
        memcpy(&s_sta_config, &s_fallback_config, sizeof(s_sta_config));
        s_sta_config_valid = true;
        memcpy(s_current_ssid, s_fallback_config.sta.ssid, sizeof(s_current_ssid));
    }

    s_wifi_state = WIFI_STATE_FAILED;

    if (s_rollback_timer) {
        esp_timer_delete(s_rollback_timer);
        s_rollback_timer = NULL;
    }

    wifi_restart_sta();
    LOGI("[WiFi]", "Rollback complete, reconnected to %s", s_current_ssid);
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

// ---------- 热切换到新 AP ----------
int wifi_switch_ap(const char *ssid, const char *password) {
    if (!ssid || !password) {
        LOGE("[WiFi]", "wifi_switch_ap: invalid params");
        return -1;
    }

    if (s_wifi_state == WIFI_STATE_SWITCHING) {
        LOGE("[WiFi]", "wifi_switch_ap: already switching");
        return -1;
    }

    // 1. 保存当前配置为回退
    if (s_sta_config_valid) {
        memcpy(&s_fallback_config, &s_sta_config, sizeof(s_fallback_config));
        s_fallback_valid = true;
        LOGI("[WiFi]", "Fallback config saved: SSID=%s",
             s_fallback_config.sta.ssid);
    } else {
        // 读当前实际配置
        wifi_config_t cur;
        if (esp_wifi_get_config(WIFI_IF_STA, &cur) == ESP_OK) {
            memcpy(&s_fallback_config, &cur, sizeof(cur));
            s_fallback_valid = true;
            LOGI("[WiFi]", "Fallback config saved (from active): SSID=%s",
                 s_fallback_config.sta.ssid);
        } else {
            LOGE("[WiFi]", "No valid config to fallback to");
            return -1;
        }
    }

    // 2. 设置新配置
    if (wifi_set_sta_config(ssid, password) != 0) {
        s_fallback_valid = false;
        return -1;
    }

    // 3. 记录目标 SSID 和切换开始时间
    memcpy(s_target_ssid, ssid, 32);
    s_target_ssid[32] = '\0';
    s_switch_start_us = esp_timer_get_time();

    // 4. 启动回退定时器（30 秒）
    esp_timer_create_args_t timer_args = {
        .callback = &wifi_rollback_timer_cb,
        .name = "wifi_rollback",
    };
    esp_timer_create(&timer_args, &s_rollback_timer);
    esp_timer_start_once(s_rollback_timer, 30 * 1000 * 1000);  // 30s

    // 5. 标记状态为切换中
    s_wifi_state = WIFI_STATE_SWITCHING;

    // 6. 重启 Wi-Fi（连接新 AP）
    wifi_restart_sta();

    LOGI("[WiFi]", "Hot-switch initiated: %s -> %s (30s timeout)",
         s_fallback_config.sta.ssid, ssid);

    return 0;
}

// ---------- 获取热切换状态 ----------
int wifi_get_switch_status(wifi_switch_status_t *status) {
    if (!status) return -1;

    memset(status, 0, sizeof(wifi_switch_status_t));
    status->state = s_wifi_state;
    status->rollback_available = s_fallback_valid;

    memcpy(status->current_ssid, s_current_ssid, sizeof(status->current_ssid));

    if (s_wifi_state == WIFI_STATE_SWITCHING) {
        memcpy(status->new_ssid, s_target_ssid, sizeof(status->new_ssid));
        int64_t now = esp_timer_get_time();
        status->elapsed_sec = (int)((now - s_switch_start_us) / 1000000);
    }

    // 获取当前信号强度
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        status->rssi = ap_info.rssi;
    }

    return 0;
}

// ---------- 初始化 Wi-Fi ----------
void wifi_init(void) {
    // 1. 初始化网络协议栈
    esp_netif_init();
    esp_event_loop_create_default();

    // 2. 创建 STA 网络接口并缓存 MAC 地址
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif) {
        uint8_t mac[6] = {0};
        if (esp_netif_get_mac(sta_netif, mac) == ESP_OK) {
            snprintf(s_mac_str, sizeof(s_mac_str), MACSTR, MAC2STR(mac));
            printf("[WiFi] MAC address: %s\n", s_mac_str);
        }
    }

    // 3. 初始化 Wi-Fi 驱动
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // 4. 注册事件回调（仅 STA 事件）
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               &wifi_event_handler, NULL);

    // 5. 配置为 STA 模式
    esp_wifi_set_mode(WIFI_MODE_STA);

    // 6. 配置 STA（连接外部路由器）
    wifi_config_t sta_config;
    memset(&sta_config, 0, sizeof(sta_config));
    memcpy(sta_config.sta.ssid, WIFI_SSID, strlen(WIFI_SSID));
    memcpy(sta_config.sta.password, WIFI_PASSWORD, strlen(WIFI_PASSWORD));
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);

    // 7. 启动 Wi-Fi
    esp_wifi_start();

    printf("Wi-Fi initialized in STA mode\n");
    printf("  STA: connecting to %s\n", WIFI_SSID);
}
