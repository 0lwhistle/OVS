/**
 * @file sysinfo.h
 * @brief 契约 I3 系统/网络信息只读查询（[HUB] 数据中枢）
 *
 * 只读封装：net_mgr_get_status + esp_netif（ip/netmask/gw/mac）。
 * 不修改 net_mgr / wifi 驱动任何文件（three_tasks_plan §2）。
 * net_mode_t 沿用 net_mgr.h 的枚举（NET_MODE_OFF/STA/AP），本头文件
 * 经 #include 引出，WEB /api/net/info 与 LVGL bridge 消费同一套值。
 */

#ifndef SYSINFO_H
#define SYSINFO_H

#include <stdint.h>
#include <stdbool.h>
#include "net_mgr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYSINFO_OK        = 0,
    SYSINFO_ERR_PARAM = -1,
    SYSINFO_ERR_FAIL  = -2,
} sysinfo_err_t;

/** 网络信息快照（字段与 WEB /api/net/info、bridge ui_bridge_net_t 一致） */
typedef struct {
    net_mode_t mode;      /* NET_MODE_OFF / STA / AP（net_mgr.h 定义） */
    bool       switching; /* net_mgr 热切换进行中 */
    char       ssid[33];
    char       ip[16];
    char       netmask[16];
    char       gw[16];
    char       mac[18];   /* "AA:BB:CC:DD:EE:FF" */
    int8_t     rssi;      /* dBm；AP 模式填 0 */
} net_info_t;

/** 设备信息（fw 版本 / 运行时长） */
typedef struct {
    char     fw_version[32];
    uint32_t uptime_sec;
} sysinfo_dev_t;

/**
 * @brief 查询网络信息（线程安全只读）
 *
 * net_mgr 未启动/不可用时返回 SYSINFO_ERR_FAIL（消费者降级显示离线）。
 */
sysinfo_err_t sysinfo_net_get(net_info_t* out);

/** 查询设备信息（fw 版本 + 运行时长） */
sysinfo_err_t sysinfo_device_get(sysinfo_dev_t* out);

/** 初始化（幂等；app_init optional 注册入口） */
sysinfo_err_t sysinfo_init(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSINFO_H */
