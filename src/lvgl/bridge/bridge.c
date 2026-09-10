/**
 * @file bridge.c
 * @brief 桥接层实现：mock 常量 / HUB 真身（编译开关切换）
 *
 * 本文件是全工程唯一允许 include HUB 公共头（i18n/sensor_cache/sysinfo/
 * time_svc）与 audio_player.h 的地方（三_tasks_plan §2 边界）。
 *
 * mock 模式内置 zh-CN/en-US 迷你词表用于 PC 模拟器语言热切换演示；
 * 真身模式 i18n_set_language 直接生效。两套实现的函数签名完全一致，
 * 交付后仅需把 OVS_BRIDGE_HUB_REAL 置 1 重新编译。
 */

#include "bridge.h"
#include "logger.h"

#include <string.h>
#include <stdio.h>

#if !OVS_BRIDGE_HUB_REAL
#include "lvgl.h"   /* mock 时钟用 lv_tick_get */
#endif

static const char* TAG = "[BRIDGE]";

/* ========================================================================== */
/*                              mock 存储                                       */
/* ========================================================================== */

static uint8_t s_mock_volume = 80;
static char s_mock_lang[8] = "zh-CN";

/* 迷你词表（mock 演示用；真身走 assets/i18n/<lang>.json） */
typedef struct {
    const char* label;
    const char* zh;
    const char* en;
} bridge_str_t;

static const bridge_str_t s_strings[] = {
    { "APP_TITLE",      "OVS 助手",    "OVS Assistant" },
    { "HOME_TIME",      "时间",        "Time" },
    { "HOME_TEMP",      "温度",        "Temp" },
    { "HOME_HUMI",      "湿度",        "Humi" },
    { "HOME_NET",       "网络",        "Network" },
    { "HOME_NET_STA",   "WiFi 模式",   "STA mode" },
    { "HOME_NET_AP",    "热点模式",    "AP mode" },
    { "HOME_NET_OFF",   "离线",        "Offline" },
    { "HOME_UNREAD",    "未读",        "Unread" },
    { "SET_TITLE",      "设置",        "Settings" },
    { "SET_LANGUAGE",   "语言",        "Language" },
    { "SET_VOLUME",     "音量",        "Volume" },
    { "SET_DEVICE_NAME","设备名",      "Device name" },
    { "SET_STANDBY",    "待机",        "Standby" },
    { "BTN_BACK",       "返回",        "Back" },
    { "SENSOR_NO_DATA", "暂无数据",    "No data" },
    { "STANDBY_HINT",   "触摸屏幕唤醒","Touch to wake" },
};
#define BRIDGE_STR_COUNT (sizeof(s_strings) / sizeof(s_strings[0]))

/* ========================================================================== */
/*                              实现                                             */
/* ========================================================================== */

#if OVS_BRIDGE_HUB_REAL
/* ---- HUB 真身（契约 I1~I4 公共头；组件交付后启用） ---- */
#include "i18n.h"
#include "sensor_cache.h"
#include "sysinfo.h"
#include "time_svc.h"

const char* ui_bridge_tr(const char* label) {
    return i18n_get(label);
}

const char* ui_bridge_lang_get(void) {
    return i18n_current();
}

bool ui_bridge_lang_set(const char* lang) {
    return i18n_set_language(lang) == I18N_OK;
}

void ui_bridge_time_get(ui_bridge_time_t* out) {
    time_svc_tm_t tm;
    if (out && time_svc_get(&tm) == TIME_SVC_OK) {
        out->hour = tm.hour;
        out->min = tm.min;
        out->sec = tm.sec;
        out->synced = tm.synced;
    }
}

bool ui_bridge_sensor_get(ui_bridge_sensor_t* out) {
    if (!out) {
        return false;
    }
    sensor_snapshot_t snap;
    if (sensor_snapshot_get(&snap) != SENSOR_OK) {
        out->valid = false;
        out->temp_m_c = 0;
        out->humi_m_p = 0;
        out->age_ms = 0;
        return false;
    }
    out->valid = snap.valid;
    out->temp_m_c = snap.temp_m_c;
    out->humi_m_p = snap.humi_m_p;
    out->age_ms = snap.age_ms;
    return snap.valid;
}

bool ui_bridge_net_get(ui_bridge_net_t* out) {
    if (!out) {
        return false;
    }
    net_info_t info;
    if (sysinfo_net_get(&info) != SYSINFO_OK) {
        return false;
    }
    out->mode = (info.mode == 1) ? UI_NET_MODE_STA :
                (info.mode == 2) ? UI_NET_MODE_AP : UI_NET_MODE_OFF;
    out->switching = info.switching;
    snprintf(out->ssid, sizeof(out->ssid), "%s", info.ssid);
    snprintf(out->ip, sizeof(out->ip), "%s", info.ip);
    snprintf(out->netmask, sizeof(out->netmask), "%s", info.netmask);
    snprintf(out->gw, sizeof(out->gw), "%s", info.gw);
    snprintf(out->mac, sizeof(out->mac), "%s", info.mac);
    out->rssi = info.rssi;
    return true;
}

#else /* ---- mock 常量（HUB 未交付） ---- */

const char* ui_bridge_tr(const char* label) {
    if (!label) {
        return "";
    }
    int zh = (strcmp(s_mock_lang, "zh-CN") == 0);
    for (unsigned i = 0; i < BRIDGE_STR_COUNT; i++) {
        if (strcmp(s_strings[i].label, label) == 0) {
            return zh ? s_strings[i].zh : s_strings[i].en;
        }
    }
    LOGW(TAG, "label not found: %s", label);
    return label;   /* 未命中回退原文 */
}

const char* ui_bridge_lang_get(void) {
    return s_mock_lang;
}

bool ui_bridge_lang_set(const char* lang) {
    if (!lang || (strcmp(lang, "zh-CN") != 0 && strcmp(lang, "en-US") != 0)) {
        return false;
    }
    strncpy(s_mock_lang, lang, sizeof(s_mock_lang) - 1);
    LOGI(TAG, "language -> %s", s_mock_lang);
    return true;
}

void ui_bridge_time_get(ui_bridge_time_t* out) {
    if (!out) {
        return;
    }
    /* mock：LVGL 开机秒数模拟时钟（synced=false → UI 灰显） */
    uint32_t sec = lv_tick_get() / 1000u;
    out->hour = (uint8_t)((sec / 3600u) % 24u);
    out->min = (uint8_t)((sec / 60u) % 60u);
    out->sec = (uint8_t)(sec % 60u);
    out->synced = false;
}

bool ui_bridge_sensor_get(ui_bridge_sensor_t* out) {
    if (!out) {
        return false;
    }
    /* mock 常量：26.5°C / 48.2%RH */
    out->valid = true;
    out->temp_m_c = 26500;
    out->humi_m_p = 48200;
    out->age_ms = 1234;
    return true;
}

bool ui_bridge_net_get(ui_bridge_net_t* out) {
    if (!out) {
        return false;
    }
    /* mock 常量：字段与 WEB /api/net/info 一致 */
    out->mode = UI_NET_MODE_STA;
    out->switching = false;
    snprintf(out->ssid, sizeof(out->ssid), "%s", "wifi2.4g");
    snprintf(out->ip, sizeof(out->ip), "%s", "192.168.2.111");
    snprintf(out->netmask, sizeof(out->netmask), "%s", "255.255.255.0");
    snprintf(out->gw, sizeof(out->gw), "%s", "192.168.2.1");
    snprintf(out->mac, sizeof(out->mac), "%s", "AA:BB:CC:DD:EE:FF");
    out->rssi = -52;
    return true;
}

#endif /* OVS_BRIDGE_HUB_REAL */

/* ---- 音量（audio_player 真身 / mock 变量） ---- */

#if OVS_BRIDGE_AUDIO
#include "audio_player.h"

void ui_bridge_init(void) {
    audio_player_err_t err = audio_player_init();
    if (err != AUDIO_PLAYER_OK) {
        LOGW(TAG, "audio_player init failed: %d (volume degraded)", err);
    }
}

uint8_t ui_bridge_volume_get(void) {
    uint8_t vol = 0;
    audio_player_get_volume(&vol);
    return vol;
}

void ui_bridge_volume_set(uint8_t volume) {
    audio_player_set_volume(volume);
}

#else

void ui_bridge_init(void) {
    LOGI(TAG, "bridge mock mode (HUB=%s, AUDIO=off)",
         OVS_BRIDGE_HUB_REAL ? "real" : "mock");
}

uint8_t ui_bridge_volume_get(void) {
    return s_mock_volume;
}

void ui_bridge_volume_set(uint8_t volume) {
    s_mock_volume = volume > 100 ? 100 : volume;
    LOGI(TAG, "volume -> %u (mock)", s_mock_volume);
}

#endif /* OVS_BRIDGE_AUDIO */

/* ---- 设备名（占位，两模式一致） ---- */

void ui_bridge_device_name_get(char* out, size_t cap) {
    if (!out || cap == 0) {
        return;
    }
    strncpy(out, "OVS-0001", cap - 1);
    out[cap - 1] = '\0';
}
