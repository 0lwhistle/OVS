/**
 * @file net_provision.h
 * @brief 配网通道提供者接口（平台无关）
 *
 * 一个 provider 代表一种"把 WiFi 凭据送进设备"的通道：
 *   - web: Web 页面/REST API（已实现，web 模块注册）
 *   - ble: 蓝牙配网（预留，蓝牙模块实现后注册）
 *
 * 通道只负责收凭据，统一经 net_provision_submit() 提交；
 * 连接结果通过 event_bus 的 EVENT_WIFI_* 事件广播，
 * 各通道可自行订阅以便向用户反馈。
 */

#ifndef NET_PROVISION_H
#define NET_PROVISION_H

#include "net_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 配网通道提供者接口 */
typedef struct net_provision_provider {
    const char *name;          /**< 通道名（"web"/"ble"，唯一） */
    net_err_t (*start)(void);  /**< 激活通道（异步，可为 NULL 表示常开） */
    net_err_t (*stop)(void);   /**< 停用通道（可为 NULL） */
} net_provision_provider_t;

/**
 * @brief 注册配网通道（最多 NET_PROVISION_MAX_PROVIDERS 个）
 */
net_err_t net_provision_register(const net_provision_provider_t *provider);

/**
 * @brief 注销配网通道
 */
net_err_t net_provision_unregister(const char *name);

/**
 * @brief 激活指定配网通道（调其 start；NULL start 视为常开直接成功）
 */
net_err_t net_provision_start(const char *name);

/**
 * @brief 停用指定配网通道
 */
net_err_t net_provision_stop(const char *name);

/**
 * @brief 配网凭据统一提交入口（任何通道收到凭据后调用）
 *
 * 行为：持久化到 NVS → 切换 STA 连接新 AP；
 * 若当前处于 AP 配网模式，设备将离开 AP 切到 STA。
 * 30s 内未连接成功，WiFi 驱动自动回退旧 AP，
 * 随后 NVS 凭据也会回退。
 */
net_err_t net_provision_submit(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif

#endif /* NET_PROVISION_H */
