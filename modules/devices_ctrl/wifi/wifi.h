#ifndef WIFI_H
#define WIFI_H

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

#endif // WIFI_H
