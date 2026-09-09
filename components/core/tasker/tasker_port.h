/**
 * @file tasker_port.h
 * @brief tasker 平台移植层（内部头）
 *
 * ESP: esp_timer 真实实现（任务超时检测）
 * PC:  计时用 CLOCK_MONOTONIC；超时定时器为无操作（timeout 特性在真机验证）
 */

#ifndef TASKER_PORT_H
#define TASKER_PORT_H

#include <stdint.h>

#if defined(ESP_PLATFORM)
#include "esp_timer.h"
typedef esp_timer_handle_t tasker_timer_t;
#else
typedef void* tasker_timer_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 毫秒时基（单调） */
uint64_t tasker_now_ms(void);

/* 一次性超时定时器（us 精度；PC 端 no-op 返回 0） */
int  tasker_timer_create(tasker_timer_t* timer, void (*cb)(void*), void* arg);
int  tasker_timer_start_once(tasker_timer_t timer, uint64_t us);
void tasker_timer_stop(tasker_timer_t timer);
void tasker_timer_delete(tasker_timer_t timer);

#ifdef __cplusplus
}
#endif

#endif /* TASKER_PORT_H */
