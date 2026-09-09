/**
 * @file bridge_time.h
 * @brief 时间桥接接口（UI 层唯一的时间来源，平台实现见 port/ 或 sim/）
 *
 * UI 六层只依赖本头文件；ESP 端由 port/bridge_time_esp.c 实现（骨架为
 * 开机 uptime），PC 模拟器由 sim/bridge_time_sim.c 实现（真实系统时间）。
 * TODO: time_srv（SNTP + 本地时钟）就绪后替换 ESP 端实现。
 */

#ifndef BRIDGE_TIME_H
#define BRIDGE_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取当前时间
 * @param hour 分钟 秒 输出指针，可为 NULL（不关心该字段）
 */
void bridge_time_get(uint8_t* hour, uint8_t* min, uint8_t* sec);

#ifdef __cplusplus
}
#endif

#endif /* BRIDGE_TIME_H */
