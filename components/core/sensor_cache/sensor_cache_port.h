/**
 * @file sensor_cache_port.h
 * @brief 传感器缓存平台移植层（锁 + 单调时基）
 */

#ifndef SENSOR_CACHE_PORT_H
#define SENSOR_CACHE_PORT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sensor_lock sensor_lock_t;

sensor_lock_t* sensor_lock_create(void);
bool sensor_lock_take(sensor_lock_t* lock, uint32_t timeout_ms);
void sensor_lock_give(sensor_lock_t* lock);
void sensor_lock_destroy(sensor_lock_t* lock);

/** 开机以来的毫秒数（单调时基，计算 age_ms 用） */
uint64_t sensor_tick_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_CACHE_PORT_H */
