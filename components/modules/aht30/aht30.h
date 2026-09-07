/**
 * @file aht30.h
 * @brief AHT30 温湿度传感器模块
 * 
 * 提供AHT30温湿度传感器的初始化、数据读取功能。
 * 使用I2C总线通信，通过event_bus发布温湿度数据。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef AHT30_H
#define AHT30_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    AHT30_OK = 0,
    AHT30_ERR_NOT_INIT = -1,
    AHT30_ERR_PARAM = -2,
    AHT30_ERR_I2C = -3,
    AHT30_ERR_CRC = -4,
    AHT30_ERR_BUSY = -5,
} aht30_err_t;

/* ========== 类型定义 ========== */

/**
 * @brief AHT30数据结构
 */
typedef struct {
    float temperature;      /**< 温度 (°C) */
    float humidity;         /**< 湿度 (%RH) */
    uint32_t timestamp;     /**< 时间戳 (ms) */
} aht30_data_t;

/**
 * @brief AHT30回调函数类型
 * 
 * @param data 温湿度数据
 * @param user_data 用户数据
 */
typedef void (*aht30_data_cb_t)(const aht30_data_t* data, void* user_data);

/* ========== 公共 API ========== */

/**
 * @brief 初始化AHT30传感器
 * 
 * 初始化I2C总线和AHT30传感器硬件。
 * 
 * @return AHT30_OK 成功
 * @return AHT30_ERR_I2C I2C初始化失败
 */
aht30_err_t aht30_init(void);

/**
 * @brief 反初始化AHT30传感器
 * 
 * @return AHT30_OK 成功
 */
aht30_err_t aht30_deinit(void);

/**
 * @brief 读取温湿度数据
 * 
 * @param data 输出参数，存储温湿度数据
 * @return AHT30_OK 成功
 * @return AHT30_ERR_NOT_INIT 未初始化
 * @return AHT30_ERR_I2C I2C读取失败
 * @return AHT30_ERR_CRC CRC校验失败
 */
aht30_err_t aht30_read(aht30_data_t* data);

/**
 * @brief 获取温度值
 * 
 * @param temperature 输出参数，存储温度值
 * @return AHT30_OK 成功
 * @return AHT30_ERR_NOT_INIT 未初始化
 */
aht30_err_t aht30_get_temperature(float* temperature);

/**
 * @brief 获取湿度值
 * 
 * @param humidity 输出参数，存储湿度值
 * @return AHT30_OK 成功
 * @return AHT30_ERR_NOT_INIT 未初始化
 */
aht30_err_t aht30_get_humidity(float* humidity);

/**
 * @brief 检查AHT30是否已初始化
 * 
 * @return true 已初始化
 * @return false 未初始化
 */
bool aht30_is_initialized(void);

/**
 * @brief 启动周期性数据采集任务
 * 
 * 使用tasker注册周期任务，定期读取数据并通过event_bus发布。
 * 
 * @param interval_ms 采集间隔 (ms)
 * @return AHT30_OK 成功
 * @return AHT30_ERR_NOT_INIT 未初始化
 */
aht30_err_t aht30_start_periodic_read(uint32_t interval_ms);

/**
 * @brief 停止周期性数据采集任务
 * 
 * @return AHT30_OK 成功
 */
aht30_err_t aht30_stop_periodic_read(void);

#ifdef __cplusplus
}
#endif

#endif /* AHT30_H */
