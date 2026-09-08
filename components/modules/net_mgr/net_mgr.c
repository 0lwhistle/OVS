/**
 * @file net_mgr.c
 * @brief 网络管理器实现
 *
 * 配置来源优先级（STA 凭据）：
 *   显式 config 参数 > NVS（用户配网）> 设备树 wifi.sta（播种 NVS）> 无
 * 设备树节点: "wifi"（见 components/dtbs/config/ovs.dtb.json）
 */

#include "net_mgr.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs.h"
#include "mdns.h"

#include "wifi.h"
#include "dtree.h"
#include "logger.h"
#include "event_bus.h"
#include "event_bus_types.h"

static const char *TAG = "[NET]";

#define NET_NVS_NAMESPACE   "net_mgr"
#define OVS_MDNS_HOSTNAME   "ovs"
#define NET_NVS_KEY_SSID     "sta_ssid"
#define NET_NVS_KEY_PASS     "sta_pass"
#define NET_NVS_KEY_DT_HASH  "dt_wifi_hash"   /* 已应用的设备树 WiFi 配置哈希 */

#define NET_PROVISION_MAX_PROVIDERS 4
#define NET_SWITCH_TIMEOUT_MS       30000  /* 热切换超时（RAM+NVS 一并回退） */

/* 设备树缺省时的 AP 兜底参数 */
#define NET_FALLBACK_AP_PREFIX  "OVS-"
#define NET_FALLBACK_AP_CHANNEL 6
#define NET_FALLBACK_AP_MAXCONN 4

// ---------- 内部状态 ----------
static bool s_initialized = false;
static net_mode_t s_mode = NET_MODE_OFF;
static net_state_t s_state = NET_STATE_IDLE;
static SemaphoreHandle_t s_lock = NULL;

static char s_sta_ssid[33] = {0};
static char s_sta_pass[65] = {0};
static char s_ap_ssid[33] = {0};
static char s_ap_pass[65] = {0};
static uint8_t s_ap_channel = 0;
static uint8_t s_ap_max_conn = 0;

// 热切换状态（net_mgr 拥有策略，驱动只执行）
static bool s_switching = false;
static int64_t s_switch_start_us = 0;
static char s_switch_prev_ssid[33] = {0};
static char s_switch_prev_pass[65] = {0};

// 配网通道注册表
static const net_provision_provider_t *s_providers[NET_PROVISION_MAX_PROVIDERS] = {0};

// 切换回退定时器（超时回退 RAM 配置 + NVS）
static TimerHandle_t s_switch_timer = NULL;

// ---------- 工具 ----------
static void net_lock(void) {
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void net_unlock(void) {
    if (s_lock) xSemaphoreGive(s_lock);
}

const char *net_err_to_str(net_err_t err) {
    switch (err) {
        case NET_OK:                return "ok";
        case NET_ERR_INVALID_PARAM: return "invalid param";
        case NET_ERR_NOT_INITIALIZED: return "not initialized";
        case NET_ERR_INVALID_STATE: return "invalid state";
        case NET_ERR_NVS:           return "nvs error";
        case NET_ERR_WIFI:          return "wifi error";
        case NET_ERR_TIMEOUT:       return "timeout";
        case NET_ERR_NO_MEMORY:     return "no memory";
        case NET_ERR_NOT_FOUND:     return "not found";
        case NET_ERR_FULL:          return "registry full";
        default:                    return "unknown";
    }
}

const char *net_mode_to_str(net_mode_t mode) {
    switch (mode) {
        case NET_MODE_STA: return "sta";
        case NET_MODE_AP:  return "ap";
        default:           return "off";
    }
}

const char *net_state_to_str(net_state_t state) {
    switch (state) {
        case NET_STATE_CONNECTING: return "connecting";
        case NET_STATE_CONNECTED:  return "connected";
        case NET_STATE_AP_RUNNING: return "ap_running";
        case NET_STATE_FAILED:     return "failed";
        default:                   return "idle";
    }
}


// ---------- 设备树 WiFi 配置哈希（FNV-1a 32bit） ----------
static uint32_t dt_wifi_hash(const char *ssid, const char *pass) {
    uint32_t h = 2166136261u;
    const char *p = ssid;
    while (*p) { h ^= (uint8_t)*p++; h *= 16777619u; }
    h ^= 0xFF; h *= 16777619u;
    p = pass ? pass : "";
    while (*p) { h ^= (uint8_t)*p++; h *= 16777619u; }
    return h;
}

static net_err_t nvs_get_dt_hash(uint32_t *out) {
    nvs_handle_t h;
    *out = 0;   /* 0 = 从未应用过设备树配置 */
    if (nvs_open(NET_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return NET_OK;
    nvs_get_u32(h, NET_NVS_KEY_DT_HASH, out);
    nvs_close(h);
    return NET_OK;
}

static net_err_t nvs_set_dt_hash(uint32_t hash) {
    nvs_handle_t h;
    if (nvs_open(NET_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return NET_ERR_NVS;
    net_err_t ret = NET_OK;
    if (nvs_set_u32(h, NET_NVS_KEY_DT_HASH, hash) != ESP_OK ||
        nvs_commit(h) != ESP_OK) ret = NET_ERR_NVS;
    nvs_close(h);
    return ret;
}

// ---------- NVS 凭据持久化 ----------
static net_err_t nvs_save_sta_creds(const char *ssid, const char *pass) {
    nvs_handle_t h;
    if (nvs_open(NET_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return NET_ERR_NVS;
    }
    net_err_t ret = NET_OK;
    if (nvs_set_str(h, NET_NVS_KEY_SSID, ssid) != ESP_OK ||
        nvs_set_str(h, NET_NVS_KEY_PASS, pass ? pass : "") != ESP_OK ||
        nvs_commit(h) != ESP_OK) {
        LOGE(TAG, "NVS save sta creds failed");
        ret = NET_ERR_NVS;
    }
    nvs_close(h);
    return ret;
}

static net_err_t nvs_load_sta_creds(char *ssid, size_t ssid_sz,
                                    char *pass, size_t pass_sz,
                                    bool *found) {
    nvs_handle_t h;
    *found = false;
    if (nvs_open(NET_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return NET_OK;  // 命名空间不存在 = 首次启动
    }
    size_t len = ssid_sz;
    size_t len2 = pass_sz;
    esp_err_t e1 = nvs_get_str(h, NET_NVS_KEY_SSID, ssid, &len);
    esp_err_t e2 = nvs_get_str(h, NET_NVS_KEY_PASS, pass, &len2);
    nvs_close(h);
    if (e1 == ESP_OK && ssid[0] != '\0') {
        if (e2 != ESP_OK) pass[0] = '\0';
        *found = true;
    }
    return NET_OK;
}

// ---------- 设备树配置读取 ----------
/**
 * @brief 从设备树 "wifi" 节点读取默认配置
 * @return true 节点存在（sta 凭据写入 ssid/pass，可为空串表示节点无此配置）
 */
static bool dtree_load_net_config(char *sta_ssid, size_t ssid_sz,
                                  char *sta_pass, size_t pass_sz,
                                  char *ap_prefix, size_t prefix_sz,
                                  char *ap_pass, size_t ap_pass_sz,
                                  uint8_t *ap_ch, uint8_t *ap_max) {
    if (!dtree_has_node("wifi")) {
        return false;
    }

    dtree_node_t *sta = dtree_get_node("wifi.sta");
    if (sta) {
        const char *v = NULL;
        if (dtree_get_string(sta, "ssid", &v) == DTREE_OK && v) {
            snprintf(sta_ssid, ssid_sz, "%s", v);
        }
        v = NULL;
        if (dtree_get_string(sta, "password", &v) == DTREE_OK && v) {
            snprintf(sta_pass, pass_sz, "%s", v);
        }
    }

    dtree_node_t *ap = dtree_get_node("wifi.ap");
    if (ap) {
        const char *v = NULL;
        if (dtree_get_string(ap, "ssid_prefix", &v) == DTREE_OK && v) {
            snprintf(ap_prefix, prefix_sz, "%s", v);
        }
        v = NULL;
        if (dtree_get_string(ap, "password", &v) == DTREE_OK && v) {
            snprintf(ap_pass, ap_pass_sz, "%s", v);
        }
        int32_t n = 0;
        if (dtree_get_int(ap, "channel", &n) == DTREE_OK && n > 0) {
            *ap_ch = (uint8_t)n;
        }
        n = 0;
        if (dtree_get_int(ap, "max_connection", &n) == DTREE_OK && n > 0) {
            *ap_max = (uint8_t)n;
        }
    }

    return true;
}

// ---------- IDF 事件回调（event loop 任务上下文） ----------
static void net_idf_event_handler(void *arg, esp_event_base_t base,
                                  int32_t id, void *data) {
    (void)arg; (void)data;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        bool was_connected = false;
        net_lock();
        was_connected = (s_state == NET_STATE_CONNECTED);
        if (s_mode == NET_MODE_STA) {
            s_state = NET_STATE_CONNECTING;  // 驱动会无限自动重连
        }
        net_unlock();
        if (was_connected) {
            LOGW(TAG, "STA disconnected, auto-reconnecting...");
            EVENT_BUS_PUBLISH_EMPTY(EVENT_WIFI_DISCONNECTED);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        net_lock();
        s_state = NET_STATE_CONNECTED;
        bool switching_done = s_switching;
        s_switching = false;
        net_unlock();
        LOGI(TAG, "STA connected, IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        EVENT_BUS_PUBLISH_EMPTY(EVENT_WIFI_GOT_IP);
        EVENT_BUS_PUBLISH_EMPTY(EVENT_WIFI_CONNECTED);
        if (switching_done) {
            LOGI(TAG, "Hot-switch SUCCESS");
            EVENT_BUS_PUBLISH_EMPTY(EVENT_WIFI_SWITCH_DONE);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        net_lock();
        s_state = NET_STATE_AP_RUNNING;
        net_unlock();
        LOGI(TAG, "AP started (SSID=%s)", s_ap_ssid);
        EVENT_BUS_PUBLISH_EMPTY(EVENT_WIFI_AP_STARTED);
    }
}

// ---------- 热切换回退定时器（超时：RAM 配置 + NVS 一并回退） ----------
static void switch_rollback_timer_cb(TimerHandle_t timer) {
    (void)timer;
    net_lock();
    bool was_switching = s_switching;
    s_switching = false;
    net_unlock();

    if (!was_switching) {
        return;  // 已在 GOT_IP 中确认成功
    }

    LOGE(TAG, "Hot-switch timeout (%ds), rolling back to %s",
         (int)(NET_SWITCH_TIMEOUT_MS / 1000), s_switch_prev_ssid);

    // 1. 回退驱动 RAM 配置并重连旧 AP
    wifi_set_sta_config(s_switch_prev_ssid, s_switch_prev_pass);
    wifi_start_sta();

    // 2. 回退 NVS 凭据
    nvs_save_sta_creds(s_switch_prev_ssid, s_switch_prev_pass);

    // 3. 状态与事件
    net_lock();
    s_state = NET_STATE_FAILED;
    snprintf(s_sta_ssid, sizeof(s_sta_ssid), "%s", s_switch_prev_ssid);
    snprintf(s_sta_pass, sizeof(s_sta_pass), "%s", s_switch_prev_pass);
    net_unlock();
    EVENT_BUS_PUBLISH_EMPTY(EVENT_WIFI_SWITCH_FAILED);

    xTimerDelete(s_switch_timer, 0);
    s_switch_timer = NULL;
}

// ---------- 配网通道注册表 ----------
net_err_t net_provision_register(const net_provision_provider_t *provider) {
    if (!provider || !provider->name) {
        return NET_ERR_INVALID_PARAM;
    }
    net_lock();
    for (int i = 0; i < NET_PROVISION_MAX_PROVIDERS; i++) {
        if (s_providers[i] &&
            strcmp(s_providers[i]->name, provider->name) == 0) {
            net_unlock();
            return NET_ERR_INVALID_STATE;  // 重名
        }
    }
    for (int i = 0; i < NET_PROVISION_MAX_PROVIDERS; i++) {
        if (!s_providers[i]) {
            s_providers[i] = provider;
            net_unlock();
            LOGI(TAG, "Provision provider registered: %s", provider->name);
            return NET_OK;
        }
    }
    net_unlock();
    return NET_ERR_FULL;
}

net_err_t net_provision_unregister(const char *name) {
    if (!name) return NET_ERR_INVALID_PARAM;
    net_lock();
    for (int i = 0; i < NET_PROVISION_MAX_PROVIDERS; i++) {
        if (s_providers[i] && strcmp(s_providers[i]->name, name) == 0) {
            s_providers[i] = NULL;
            net_unlock();
            return NET_OK;
        }
    }
    net_unlock();
    return NET_ERR_NOT_FOUND;
}

net_err_t net_provision_start(const char *name) {
    if (!name) return NET_ERR_INVALID_PARAM;
    net_lock();
    for (int i = 0; i < NET_PROVISION_MAX_PROVIDERS; i++) {
        if (s_providers[i] && strcmp(s_providers[i]->name, name) == 0) {
            const net_provision_provider_t *p = s_providers[i];
            net_unlock();
            if (!p->start) return NET_OK;  // 常开通道
            return p->start();
        }
    }
    net_unlock();
    return NET_ERR_NOT_FOUND;
}

net_err_t net_provision_stop(const char *name) {
    if (!name) return NET_ERR_INVALID_PARAM;
    net_lock();
    for (int i = 0; i < NET_PROVISION_MAX_PROVIDERS; i++) {
        if (s_providers[i] && strcmp(s_providers[i]->name, name) == 0) {
            const net_provision_provider_t *p = s_providers[i];
            net_unlock();
            if (!p->stop) return NET_OK;
            return p->stop();
        }
    }
    net_unlock();
    return NET_ERR_NOT_FOUND;
}

net_err_t net_provision_submit(const char *ssid, const char *password) {
    if (!ssid || ssid[0] == '\0' || strlen(ssid) > 32 ||
        (password && strlen(password) > 64)) {
        return NET_ERR_INVALID_PARAM;
    }
    if (!s_initialized) return NET_ERR_NOT_INITIALIZED;

    LOGI(TAG, "Provisioning submit: SSID=%s", ssid);

    // 1. 备份当前凭据（回退用）
    snprintf(s_switch_prev_ssid, sizeof(s_switch_prev_ssid), "%s", s_sta_ssid);
    snprintf(s_switch_prev_pass, sizeof(s_switch_prev_pass), "%s", s_sta_pass);

    // 2. 应用新凭据到内存 + NVS
    snprintf(s_sta_ssid, sizeof(s_sta_ssid), "%s", ssid);
    snprintf(s_sta_pass, sizeof(s_sta_pass), "%s", password ? password : "");
    net_err_t ret = nvs_save_sta_creds(s_sta_ssid, s_sta_pass);
    if (ret != NET_OK) {
        return ret;
    }

    // 3. 启动切换回退定时器
    if (s_switch_timer) {
        xTimerStop(s_switch_timer, 0);
        xTimerDelete(s_switch_timer, 0);
        s_switch_timer = NULL;
    }
    s_switch_timer = xTimerCreate(
        "net_switch", pdMS_TO_TICKS(NET_SWITCH_TIMEOUT_MS),
        pdFALSE, NULL, switch_rollback_timer_cb);
    if (!s_switch_timer) {
        LOGW(TAG, "switch timer create failed (no auto-rollback)");
    }

    // 4. 经驱动连接新 AP（轻量重启路径）
    net_lock();
    s_switching = true;
    s_switch_start_us = esp_timer_get_time();
    s_mode = NET_MODE_STA;
    s_state = NET_STATE_CONNECTING;
    net_unlock();

    EVENT_BUS_PUBLISH_EMPTY(EVENT_WIFI_SWITCH_START);
    if (wifi_set_sta_config(s_sta_ssid, s_sta_pass) != 0 ||
        wifi_start_sta() != 0) {
        LOGE(TAG, "wifi switch initiation failed");
        net_lock();
        s_switching = false;
        net_unlock();
        return NET_ERR_WIFI;
    }

    return NET_OK;
}

// ---------- 管理器核心 ----------
net_err_t net_mgr_init(const net_config_t *cfg) {
    if (s_initialized) {
        LOGW(TAG, "Already initialized");
        return NET_OK;
    }

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return NET_ERR_NO_MEMORY;

    // AP 兜底参数（设备树缺失时使用）
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s", NET_FALLBACK_AP_PREFIX);
    s_ap_channel = NET_FALLBACK_AP_CHANNEL;
    s_ap_max_conn = NET_FALLBACK_AP_MAXCONN;

    // 1. 设备树默认配置（STA 凭据 + AP 参数）
    char dt_ssid[33] = {0}, dt_pass[65] = {0};
    char dt_ap_prefix[33] = {0}, dt_ap_pass[65] = {0};
    uint8_t dt_ap_ch = 0, dt_ap_max = 0;
    bool has_dtree = dtree_load_net_config(
        dt_ssid, sizeof(dt_ssid), dt_pass, sizeof(dt_pass),
        dt_ap_prefix, sizeof(dt_ap_prefix), dt_ap_pass, sizeof(dt_ap_pass),
        &dt_ap_ch, &dt_ap_max);

    /* 2. STA 凭据来源：
     *    显式 config > 设备树变更覆盖（烧录新设备树=配置哈希变化→覆盖 NVS）
     *                > NVS（用户配网，跨 OTA 幸存） > 无 */
    bool found = false;
    nvs_load_sta_creds(s_sta_ssid, sizeof(s_sta_ssid),
                       s_sta_pass, sizeof(s_sta_pass), &found);
    if (cfg && cfg->sta_ssid) {
        snprintf(s_sta_ssid, sizeof(s_sta_ssid), "%s", cfg->sta_ssid);
        snprintf(s_sta_pass, sizeof(s_sta_pass), "%s", cfg->sta_password ? cfg->sta_password : "");
        nvs_save_sta_creds(s_sta_ssid, s_sta_pass);
        LOGI(TAG, "STA credentials overridden by config");
    } else if (has_dtree && dt_ssid[0] != '\0') {
        uint32_t dt_hash = dt_wifi_hash(dt_ssid, dt_pass);
        uint32_t applied_hash = 0;
        nvs_get_dt_hash(&applied_hash);
        if (dt_hash != applied_hash) {
            /* 设备树 WiFi 配置变更（新设备树烧录）：以设备树为准覆盖 NVS。
             * 用户此后仍可经配网接口覆盖 NVS，设备树再次变更前一直生效 */
            snprintf(s_sta_ssid, sizeof(s_sta_ssid), "%s", dt_ssid);
            snprintf(s_sta_pass, sizeof(s_sta_pass), "%s", dt_pass);
            nvs_save_sta_creds(s_sta_ssid, s_sta_pass);
            nvs_set_dt_hash(dt_hash);
            LOGI(TAG, "Device tree WiFi config APPLIED (overrides NVS): %s", s_sta_ssid);
        } else if (found) {
            LOGI(TAG, "STA credentials from NVS (dtree unchanged): %s", s_sta_ssid);
        } else {
            LOGW(TAG, "NVS empty though dtree applied before, re-applying: %s", dt_ssid);
            snprintf(s_sta_ssid, sizeof(s_sta_ssid), "%s", dt_ssid);
            snprintf(s_sta_pass, sizeof(s_sta_pass), "%s", dt_pass);
            nvs_save_sta_creds(s_sta_ssid, s_sta_pass);
        }
    } else if (found) {
        LOGI(TAG, "STA credentials from NVS (no device tree wifi.sta): %s", s_sta_ssid);
    } else {
        LOGW(TAG, "No STA credentials (NVS empty, no device tree wifi.sta)");
    }

    // 3. AP 参数：显式 config > 设备树 > 兜底
    if (cfg && cfg->ap_ssid) {
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s", cfg->ap_ssid);
    } else if (has_dtree && dt_ap_prefix[0] != '\0') {
        uint8_t mac[6] = {0};
        char prefix[29];         /* 预留 4 字节 MAC 后缀, 全程不截断 */
        size_t plen = strlen(dt_ap_prefix);
        if (plen > sizeof(prefix) - 1) plen = sizeof(prefix) - 1;
        memcpy(prefix, dt_ap_prefix, plen);
        prefix[plen] = '\0';
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s%02X%02X",
                 prefix, mac[4], mac[5]);
    }
    if (cfg && cfg->ap_password) {
        snprintf(s_ap_pass, sizeof(s_ap_pass), "%s", cfg->ap_password);
    } else if (has_dtree && dt_ap_pass[0] != '\0') {
        snprintf(s_ap_pass, sizeof(s_ap_pass), "%s", dt_ap_pass);
    }
    if (cfg && cfg->ap_channel) {
        s_ap_channel = cfg->ap_channel;
    } else if (dt_ap_ch) {
        s_ap_channel = dt_ap_ch;
    }
    if (cfg && cfg->ap_max_conn) {
        s_ap_max_conn = cfg->ap_max_conn;
    } else if (dt_ap_max) {
        s_ap_max_conn = dt_ap_max;
    }

    // 4. WiFi 协议栈准备（不启动射频）
    wifi_init();

    // 5. mDNS 主机名 ovs.local（STA/AP 模式均广播 _http._tcp）
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(OVS_MDNS_HOSTNAME);
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);  /* Web 端口 80 */
        LOGI(TAG, "mDNS hostname: %s.local", OVS_MDNS_HOSTNAME);
    } else {
        LOGW(TAG, "mDNS init failed (name resolution disabled)");
    }

    // 6. 注册网络事件回调
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                               &net_idf_event_handler, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_START,
                               &net_idf_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               &net_idf_event_handler, NULL);

    s_initialized = true;
    LOGI(TAG, "Network manager initialized (AP default SSID=%s, ch=%d, dtree=%s)",
         s_ap_ssid, s_ap_channel, has_dtree ? "yes" : "no");
    return NET_OK;
}

net_err_t net_mgr_start(net_mode_t mode) {
    if (!s_initialized) return NET_ERR_NOT_INITIALIZED;
    if (mode != NET_MODE_STA && mode != NET_MODE_AP && mode != NET_MODE_OFF) {
        return NET_ERR_INVALID_PARAM;
    }

    net_lock();
    if (mode == s_mode &&
        (mode != NET_MODE_STA || s_state == NET_STATE_CONNECTED)) {
        net_unlock();
        return NET_OK;  // 已在目标模式
    }
    net_unlock();

    if (mode == NET_MODE_OFF) {
        return net_mgr_stop();
    }

    if (mode == NET_MODE_STA) {
        if (s_sta_ssid[0] == '\0') {
            LOGE(TAG, "STA start failed: no credentials");
            return NET_ERR_INVALID_STATE;
        }
        if (wifi_set_sta_config(s_sta_ssid, s_sta_pass) != 0 ||
            wifi_start_sta() != 0) {
            LOGE(TAG, "STA start failed");
            return NET_ERR_WIFI;
        }
        net_lock();
        s_mode = NET_MODE_STA;
        s_state = NET_STATE_CONNECTING;
        net_unlock();
        LOGI(TAG, "STA starting (SSID=%s)", s_sta_ssid);
    } else {
        if (wifi_start_ap(s_ap_ssid, s_ap_pass, s_ap_channel, s_ap_max_conn) != 0) {
            LOGE(TAG, "AP start failed");
            return NET_ERR_WIFI;
        }
        net_lock();
        s_mode = NET_MODE_AP;
        s_state = NET_STATE_AP_RUNNING;
        net_unlock();
    }

    return NET_OK;
}

net_err_t net_mgr_stop(void) {
    if (!s_initialized) return NET_ERR_NOT_INITIALIZED;
    if (wifi_stop() != 0) return NET_ERR_WIFI;
    net_lock();
    s_mode = NET_MODE_OFF;
    s_state = NET_STATE_IDLE;
    net_unlock();
    return NET_OK;
}

net_err_t net_mgr_get_status(net_status_t *out) {
    if (!out) return NET_ERR_INVALID_PARAM;
    if (!s_initialized) return NET_ERR_NOT_INITIALIZED;

    memset(out, 0, sizeof(*out));

    net_lock();
    out->mode = s_mode;
    out->state = s_state;
    snprintf(out->ssid, sizeof(out->ssid), "%s",
             s_mode == NET_MODE_AP ? s_ap_ssid : s_sta_ssid);
    out->sta_configured = (s_sta_ssid[0] != '\0');
    out->switching = s_switching;
    if (s_switching) {
        out->switch_elapsed_sec =
            (int)((esp_timer_get_time() - s_switch_start_us) / 1000000);
    }
    net_unlock();

    if (s_mode == NET_MODE_STA) {
        wifi_net_info_t info;
        if (wifi_get_net_info(&info) == 0) {
            snprintf(out->ip, sizeof(out->ip), "%s", info.ip);
        }
        out->rssi = wifi_get_rssi();
    } else if (s_mode == NET_MODE_AP) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("AP_DEF");
        if (netif) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                snprintf(out->ip, sizeof(out->ip), IPSTR,
                         IP2STR(&ip_info.ip));
            }
        }
    }

    return NET_OK;
}
