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

// ---------- Wi-Fi STA 默认配置 ----------
#define WIFI_SSID       "816"
#define WIFI_PASSWORD   "716717nb"

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

// ---------- Wi-Fi 热切换状态 ----------
typedef enum {
    WIFI_STATE_IDLE,          // 空闲
    WIFI_STATE_SCANNING,      // 正在扫描
    WIFI_STATE_SWITCHING,     // 正在切换到新 AP
    WIFI_STATE_CONNECTED,     // 已连接
    WIFI_STATE_FAILED,        // 切换失败（已回退）
} wifi_state_t;

// ---------- Wi-Fi 热切换状态详情 ----------
typedef struct {
    wifi_state_t state;         // 当前状态
    char current_ssid[33];      // 当前连接的 SSID
    char new_ssid[33];          // 目标 SSID（SWITCHING 时有值）
    int rssi;                   // 当前信号强度 (dBm)
    int elapsed_sec;            // 切换已用时间（秒）
    bool rollback_available;    // 是否有回退配置
} wifi_switch_status_t;

// ---------- Wi-Fi 事件回调（IDF 方式） ----------
void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

// ---------- 初始化 Wi-Fi（仅 STA 模式） ----------
void wifi_init(void);

/**
 * @brief 设置 STA 的 SSID 和密码（仅修改内存中的配置，不立即生效）
 * @param ssid    目标 SSID（最长 32 字节）
 * @param password 目标密码（最长 64 字节）
 * @return 0 成功，-1 参数无效
 */
int wifi_set_sta_config(const char *ssid, const char *password);

/**
 * @brief 完全重启 Wi-Fi STA 网络
 * 
 * 执行完整的清理和刷新流程：
 *   1. 断开当前连接
 *   2. 停止 Wi-Fi 驱动
 *   3. 清理并释放 netif 和 event loop
 *   4. 重新初始化 netif、event loop、Wi-Fi 驱动
 *   5. 应用最新的 STA 配置
 *   6. 重新启动 Wi-Fi 并连接
 */
void wifi_restart_sta(void);

/**
 * @brief 获取当前 Wi-Fi STA 的网络信息（IP、子网掩码、网关、MAC）
 * @param info 输出参数，填充网络信息
 * @return 0 成功，-1 获取失败（如未连接）
 */
int wifi_get_net_info(wifi_net_info_t *info);


/**
 * @brief 扫描附近 Wi-Fi 热点（同步阻塞，最长 5 秒）
 * 
 * @param aps    输出数组，结果按信号强度降序排列
 * @param max_ap 最多返回数量
 * @return 实际扫描到的 AP 数量，失败返回 0
 */
int wifi_scan_aps(wifi_ap_info_t *aps, int max_ap);

/**
 * @brief 热切换到新 AP（30 秒超时自动回退）
 * 
 * 保存当前配置为回退 → 断开并连接新 AP →
 *   成功（30s 内获取 IP）→ 清除回退，持久化新配置
 *   失败（30s 超时）    → 自动恢复旧配置
 * 
 * @return 0 发起成功，-1 已有切换在进行中
 */
int wifi_switch_ap(const char *ssid, const char *password);

/**
 * @brief 获取当前热切换状态（用于前端轮询）
 * @return 0 成功
 */
int wifi_get_switch_status(wifi_switch_status_t *status);

#endif // WIFI_H
