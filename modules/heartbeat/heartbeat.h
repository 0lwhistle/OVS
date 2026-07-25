/**
 * @brief 心跳任务模块
 * 
 * 每秒执行一次，更新 WiFi 信号强度和运行时间。
 * 通过 tasker 系统周期调度。
 */
#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#include <stdint.h>

/**
 * @brief 初始化心跳任务
 * 
 * 注册一个每秒执行一次的周期任务到 tasker。
 * 在 app_main 中调用一次。
 * 
 * @return 0 成功，-1 失败
 */
int heartbeat_init(void);

/**
 * @brief 获取当前 WiFi 信号强度
 * @return RSSI (dBm)，失败返回 0
 */
int heartbeat_get_rssi(void);

/**
 * @brief 获取系统运行时间
 * @return 秒数
 */
uint64_t heartbeat_get_uptime_sec(void);

#endif // HEARTBEAT_H