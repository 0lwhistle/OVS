/**
 * @file sensor_cache.h
 * @brief 契约 I2 传感器快照缓存（[HUB] 数据中枢）
 *
 * 内部订阅 EVENT_SENSOR_TEMP_HUMIDITY / EVENT_SENSOR_ERROR（event_bus），
 * 缓存最新采样（milli 单位）；get 返回快照 + age_ms，线程安全只读。
 * 消费者：LVGL bridge（ui_bridge_sensor_get）、Web /api/sensor 路由。
 */

#ifndef SENSOR_CACHE_H
#define SENSOR_CACHE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 错误码（负值=错误） */
typedef enum {
    SENSOR_OK        = 0,
    SENSOR_ERR_PARAM = -1,
    SENSOR_ERR_FAIL  = -2,
} sensor_err_t;

/** 传感器快照（与 bridge ui_bridge_sensor_t / WEB /api/sensor 字段对齐） */
typedef struct {
    int32_t  temp_m_c;   /* 毫摄氏度 */
    int32_t  humi_m_p;   /* 毫%RH */
    uint32_t age_ms;     /* 距上次有效采样的毫秒数 */
    bool     valid;
} sensor_snapshot_t;

/**
 * @brief 初始化：注册 event_bus 订阅（幂等）
 *
 * optional 模块：event_bus 未就绪或订阅失败时返回错误，调用方降级
 * （消费者拿到的快照恒 valid=false）。
 */
sensor_err_t sensor_cache_init(void);

/**
 * @brief 取快照（线程安全只读）
 *
 * 从未收到过有效采样时返回 SENSOR_ERR_FAIL 且 out.valid=false。
 */
sensor_err_t sensor_snapshot_get(sensor_snapshot_t* out);

/** 释放订阅（测试用） */
void sensor_cache_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_CACHE_H */
