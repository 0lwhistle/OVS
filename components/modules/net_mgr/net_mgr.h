/**
 * @file net_mgr.h
 * @brief 网络管理器：STA/AP 模式状态机 + 凭据持久化 + 配网通道注册
 *
 * 职责：
 *   - 管理 WiFi 两种互斥模式（NET_MODE_STA / NET_MODE_AP）的启动与切换
 *   - STA 凭据持久化到 NVS（首次引导用出厂默认值播种）
 *   - 配网通道（web/蓝牙...）注册与凭据统一提交
 *   - 向 event_bus 发布 EVENT_WIFI_* 事件
 *   - 对外提供统一状态查询（供 Web API / 未来 LVGL 网络配置页）
 *
 * 线程模型：无专用线程。状态机在 IDF event loop 回调中运行，
 * 状态查询线程安全（内部互斥锁）。
 */

#ifndef NET_MGR_H
#define NET_MGR_H

#include "net_types.h"
#include "net_provision.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 网络模式（AP/STA 不共存） */
typedef enum {
    NET_MODE_OFF = 0,   /**< 射频关闭 */
    NET_MODE_STA = 1,   /**< 站点模式：连接外部路由器 */
    NET_MODE_AP  = 2,   /**< 接入点模式：设备自建热点 */
} net_mode_t;

/** 网络状态 */
typedef enum {
    NET_STATE_IDLE      = 0,  /**< 未启动 */
    NET_STATE_CONNECTING,     /**< STA 连接中（含驱动自动重连） */
    NET_STATE_CONNECTED,      /**< STA 已获取 IP */
    NET_STATE_AP_RUNNING,     /**< AP 已启动 */
    NET_STATE_FAILED,         /**< 配网切换失败已回退 */
} net_state_t;

/** 状态快照（供 Web API / LVGL 网络页读取） */
typedef struct {
    net_mode_t  mode;
    net_state_t state;
    char        ssid[33];     /**< 当前 STA 目标 / AP 名称 */
    char        ip[16];       /**< STA: 分配到的 IP；AP: 网段地址 */
    int         rssi;         /**< STA 信号强度，AP 模式为 0 */
    bool        sta_configured; /**< NVS 中是否存在 STA 凭据 */
    bool        switching;          /**< 正在切换 AP（热切换进行中） */
    int         switch_elapsed_sec; /**< 热切换已用时（秒） */
} net_status_t;

/** 初始化配置（字段为 NULL 时使用默认行为） */
typedef struct {
    const char *sta_ssid;     /**< NULL → 读 NVS，NVS 无 → 出厂默认并写入 NVS */
    const char *sta_password; /**< 与 sta_ssid 成对使用 */
    const char *ap_ssid;      /**< NULL → "OVS-xxxx"（MAC 后 4 位） */
    const char *ap_password;  /**< NULL/空 → 开放网络 */
    uint8_t     ap_channel;   /**< 0 → 默认 6 */
    uint8_t     ap_max_conn;  /**< 0 → 默认 4 */
} net_config_t;

/**
 * @brief 初始化网络管理器（幂等）
 *
 * 完成 NVS 凭据装载/播种、WiFi 协议栈准备（不启动射频）、
 * 事件回调注册。调用前需完成 nvs_flash_init()。
 */
net_err_t net_mgr_init(const net_config_t *cfg);

/**
 * @brief 启动指定模式（STA/AP 互斥，重复调用自动切换）
 */
net_err_t net_mgr_start(net_mode_t mode);

/**
 * @brief 停止网络（射频关闭，协议栈保留）
 */
net_err_t net_mgr_stop(void);

/**
 * @brief 读取状态快照（线程安全）
 */
net_err_t net_mgr_get_status(net_status_t *out);

const char *net_mode_to_str(net_mode_t mode);
const char *net_state_to_str(net_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* NET_MGR_H */
