/**
 * @file bridge.h
 * @brief 桥接层接口：UI 六层唯一的后端数据/操作入口（契约 I1~I4）
 *
 * 职责：屏蔽 HUB（i18n/sensor_cache/sysinfo/time_svc）与音频服务
 * （audio_player）。UI 层只 include 本头（层内头）；只有 bridge.c 可以
 * include HUB 公共头。
 *
 * 编译开关（由构建注入）：
 *   OVS_BRIDGE_HUB_REAL=1  → 接 HUB 真实现（未交付前=0，用 mock 常量）
 *   OVS_BRIDGE_AUDIO=1     → 音量经 audio_player 实调（ESP；PC 模拟=0）
 *
 * 多语言：_(label) 宏 → ui_bridge_tr()；未命中回退 label 原文。
 */

#ifndef BRIDGE_H
#define BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 数据结构（字段与 WEB /api/net/info 等契约一致） ========== */

/** I4 时间 */
typedef struct {
    uint8_t hour, min, sec;
    bool    synced;      /* NTP 同步标志，未同步时时间照常显示但灰显 */
} ui_bridge_time_t;

/** I2 传感器快照（milli 单位） */
typedef struct {
    bool     valid;
    int32_t  temp_m_c;   /* 毫摄氏度 */
    int32_t  humi_m_p;   /* 毫%RH */
    uint32_t age_ms;
} ui_bridge_sensor_t;

/** 网络模式（与 I3 net_mode_t 对齐） */
typedef enum {
    UI_NET_MODE_OFF = 0,
    UI_NET_MODE_STA = 1,
    UI_NET_MODE_AP  = 2,
} ui_net_mode_t;

/** I3 系统/网络信息（字段与 WEB /api/net/info 一致） */
typedef struct {
    ui_net_mode_t mode;
    bool    switching;
    char    ssid[33];
    char    ip[16];
    char    netmask[16];
    char    gw[16];
    char    mac[18];
    int8_t  rssi;        /* dBm；AP 模式填 0 */
} ui_bridge_net_t;

/* ========== 生命周期 ========== */

/** 初始化桥（接 audio_player 等；幂等） */
void ui_bridge_init(void);

/* ========== I1 多语言 ========== */

/** 翻译：未命中返回 label 原文 */
const char* ui_bridge_tr(const char* label);

/** 页面/控件统一使用本宏取文案（禁止硬编码） */
#define _(label) ui_bridge_tr(label)

/** 当前语言码（如 "zh-CN"） */
const char* ui_bridge_lang_get(void);

/** 热切换语言（成功后调用方应 navigator_reload 重建页面） */
bool ui_bridge_lang_set(const char* lang);

/* ========== I4 时间 ========== */

void ui_bridge_time_get(ui_bridge_time_t* out);

/* ========== I2 传感器 ========== */

/** 快照；无有效数据返回 false（out 仍填 valid=false 缺省值） */
bool ui_bridge_sensor_get(ui_bridge_sensor_t* out);

/* ========== I3 网络/系统信息 ========== */

bool ui_bridge_net_get(ui_bridge_net_t* out);

/* ========== 音量（批次① audio_player 成果） ========== */

/** 当前音量 0~100 */
uint8_t ui_bridge_volume_get(void);

/** 设置音量（经 audio_player_set_volume；mock 模式仅存变量） */
void ui_bridge_volume_set(uint8_t volume);

/* ========== 设备名（占位） ========== */

void ui_bridge_device_name_get(char* out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* BRIDGE_H */
