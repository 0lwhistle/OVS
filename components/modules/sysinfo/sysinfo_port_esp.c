/**
 * @file sysinfo_port_esp.c
 * @brief 系统信息平台移植层实现（ESP32：esp_netif / esp_wifi / esp_app_desc）
 */

#include "sysinfo_port.h"
#include "logger.h"

#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_app_desc.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

static const char* TAG = "[SYSINFO]";

bool sysinfo_port_get_ip_stack(bool sta, char* ip, int ip_cap,
                               char* netmask, int nm_cap,
                               char* gw, int gw_cap) {
    const char* ifkey = sta ? "WIFI_STA_DEF" : "WIFI_AP_DEF";
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey(ifkey);
    if (!netif) {
        return false;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK) {
        LOGW(TAG, "esp_netif_get_ip_info failed (%s)", ifkey);
        return false;
    }
    snprintf(ip, ip_cap, IPSTR, IP2STR(&info.ip));
    snprintf(netmask, nm_cap, IPSTR, IP2STR(&info.netmask));
    snprintf(gw, gw_cap, IPSTR, IP2STR(&info.gw));
    return true;
}

bool sysinfo_port_get_mac(char* mac, int cap) {
    uint8_t m[6] = {0};
    if (esp_wifi_get_mac(WIFI_IF_STA, m) != ESP_OK &&
        esp_wifi_get_mac(WIFI_IF_AP, m) != ESP_OK) {
        return false;
    }
    snprintf(mac, cap, "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    return true;
}

void sysinfo_port_get_fw_version(char* out, int cap) {
    const esp_app_desc_t* desc = esp_app_get_description();
    if (desc) {
        snprintf(out, cap, "%s", desc->version);
    } else {
        snprintf(out, cap, "%s", "unknown");
    }
}

uint32_t sysinfo_port_uptime_sec(void) {
    return (uint32_t)(esp_timer_get_time() / 1000000LL);
}
