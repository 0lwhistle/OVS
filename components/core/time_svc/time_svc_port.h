/**
 * @file time_svc_port.h
 * @brief 时间服务平台移植层（ESP=esp_timer / PC=POSIX 时钟）
 */

#ifndef TIME_SVC_PORT_H
#define TIME_SVC_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 开机以来的毫秒数（单调时基） */
uint64_t time_svc_port_tick_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* TIME_SVC_PORT_H */
