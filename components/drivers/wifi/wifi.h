/**
 * @file wifi.h
 * @brief WiFi 驱动（esp_wifi 纯封装层）
 *
 * 职责边界（驱动只做硬件操作，策略在 modules/net_mgr）：
 *   - 协议栈/netif/事件回调的初始化（wifi_init，不启动射频）
 *   - STA/AP 的配置、启动、停止（含 AP 参数校验）
 *   - 硬件查询：IP/网关/子网掩码/MAC(wifi_get_net_info)、RSSI、
 *     附近 AP 扫描、当前模式
 *   - 断线自动重连（驱动级保底行为）
 *
 * 不属于驱动的（在 net_mgr）：凭据持久化、配网、热切换回退策略、
 * 模式状态机、事件语义化发布。凭据来源见 net_mgr（设备树/NVS）。
 */

#ifndef WIFI_H
#define WIFI_H

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <stdbool.h>

// ---------- Wi-Fi 网络信息结构体 ----------
typedef struct {
    char ip[16];        // IP 地址（如 "192.168.1.100"）
    char netmask[16];   // 子网掩码（如 "255.255.255.0"）
    char gw[16];        // 网关（如 "192.168.1.1"）
    char mac[18];       // MAC 地址（如 "xx:xx:xx:xx:xx:xx"）
} wifi_net_info_t;

// ---------- Wi-Fi 扫描结果 ----------
typedef struct {
    char ssid[33];      // AP 名称
    int rssi;           // 信号强度 (dBm)
    int authmode;       // 加密方式: 0=OPEN, 1=WEP, 2=WPA_PSK, 3=WPA2_PSK,
                        //           4=WPA_WPA2_PSK, 5=WPA2_ENTERPRISE,
                        //           6=WPA3_PSK, 7=WPA2_WPA3_PSK
} wifi_ap_info_t;

/**
 * @brief 初始化 Wi-Fi 协议栈（不启动、不连接）
 *
 * 完成 netif/event loop/驱动初始化并注册事件回调，
 * STA/AP 网络接口均创建。启动由 net_mgr 通过
 * wifi_start_sta()/wifi_start_ap() 控制。
 */
void wifi_init(void);

/**
 * @brief 设置 STA 的 SSID 和密码（存内存，由 wifi_start_sta 应用）
 * @return 0 成功，-1 参数无效
 */
int wifi_set_sta_config(const char *ssid, const char *password);

/**
 * @brief 应用当前 STA 配置并启动连接（STA_START 事件自动 esp_wifi_connect）
 * @return 0 成功，-1 失败（无有效配置/驱动错误）
 */
int wifi_start_sta(void);

/**
 * @brief 启动 SoftAP 模式（会先停止当前 STA）
 *
 * @param ssid     AP 名称（1~32 字节）
 * @param password 密码（8~64 字节，NULL/空 = 开放网络）
 * @param channel  信道（0 = 默认 6）
 * @param max_conn 最大站点数（0 = 默认 4）
 * @return 0 成功，-1 失败
 */
int wifi_start_ap(const char *ssid, const char *password,
                  uint8_t channel, uint8_t max_conn);

/**
 * @brief 停止 Wi-Fi 射频（netif 与驱动保持初始化状态）
 * @return 0 成功，-1 失败
 */
int wifi_stop(void);

/**
 * @brief 获取当前 Wi-Fi 模式
 * @return WIFI_MODE_NULL/WIFI_MODE_STA/WIFI_MODE_AP，失败返回 WIFI_MODE_NULL
 */
wifi_mode_t wifi_get_mode(void);

/**
 * @brief 获取当前 STA 信号强度
 * @return RSSI (dBm，负值)；未连接/失败返回 0
 */
int wifi_get_rssi(void);

/**
 * @brief 扫描附近 Wi-Fi 热点（同步阻塞，最长 5 秒）
 *
 * @param aps    输出数组，结果按信号强度降序排列
 * @param max_ap 最多返回数量
 * @return 实际扫描到的 AP 数量，失败返回 0
 */
int wifi_scan_aps(wifi_ap_info_t *aps, int max_ap);

/**
 * @brief 获取当前 Wi-Fi STA 的网络信息（IP、子网掩码、网关、MAC）
 * @param info 输出参数，填充网络信息
 * @return 0 成功，-1 获取失败（如未连接）
 */
int wifi_get_net_info(wifi_net_info_t *info);

/**
 * @brief 获取当前 STA 的 IP 地址
 * @param buf 输出缓冲区（建议 ≥16 字节）
 * @return 0 成功，-1 失败（未连接/未初始化）
 */
int wifi_get_ip(char *buf, int len);

/**
 * @brief 获取当前 STA 的网关地址
 * @param buf 输出缓冲区（建议 ≥16 字节）
 * @return 0 成功，-1 失败
 */
int wifi_get_gateway(char *buf, int len);

/**
 * @brief 获取当前 STA 的子网掩码
 * @param buf 输出缓冲区（建议 ≥16 字节）
 * @return 0 成功，-1 失败
 */
int wifi_get_netmask(char *buf, int len);

#endif // WIFI_H
