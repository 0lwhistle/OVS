/**
 * @file i2s_drv.h
 * @brief I2S驱动接口
 * 
 * 提供I2S总线初始化、配置和数据传输功能。
 * 硬件参数从设备树读取，不在代码中硬编码。
 */

#ifndef I2S_DRV_H
#define I2S_DRV_H

#include <stdint.h>
#include <stdbool.h>
#include "dtree.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    I2S_DRV_OK = 0,
    I2S_DRV_ERR_NOT_INIT = -1,
    I2S_DRV_ERR_CONFIG = -2,
    I2S_DRV_ERR_PARAM = -3,
    I2S_DRV_ERR_HW = -4,
} i2s_drv_err_t;

/* ========== 类型定义 ========== */
typedef struct i2s_drv_config {
    int32_t bclk_pin;
    int32_t ws_pin;
    int32_t data_in_pin;   // 麦克风数据引脚
    int32_t data_out_pin;  // 功放数据引脚
    int32_t sample_rate;
    int32_t data_bits;
    bool is_mono;
} i2s_drv_config_t;

typedef struct i2s_drv_handle* i2s_drv_handle_t;

/* ========== 公共 API ========== */

/**
 * @brief 从设备树加载I2S配置
 * 
 * @param config 输出参数，存储配置信息
 * @return i2s_drv_err_t 错误码
 */
i2s_drv_err_t i2s_drv_load_config(i2s_drv_config_t* config);

/**
 * @brief 初始化I2S驱动
 * 
 * @param config 配置信息
 * @param handle 输出参数，存储驱动句柄
 * @return i2s_drv_err_t 错误码
 */
i2s_drv_err_t i2s_drv_init(const i2s_drv_config_t* config, i2s_drv_handle_t* handle);

/**
 * @brief 反初始化I2S驱动
 * 
 * @param handle 驱动句柄
 * @return i2s_drv_err_t 错误码
 */
i2s_drv_err_t i2s_drv_deinit(i2s_drv_handle_t handle);

/**
 * @brief 从麦克风读取数据
 * 
 * @param handle 驱动句柄
 * @param buffer 数据缓冲区
 * @param size   缓冲区大小（字节）
 * @param bytes_read 实际读取的字节数
 * @return i2s_drv_err_t 错误码
 */
i2s_drv_err_t i2s_drv_read(i2s_drv_handle_t handle, void* buffer, size_t size, size_t* bytes_read);

/**
 * @brief 向功放写入数据
 * 
 * @param handle 驱动句柄
 * @param buffer 数据缓冲区
 * @param size   数据大小（字节）
 * @param bytes_written 实际写入的字节数
 * @return i2s_drv_err_t 错误码
 */
i2s_drv_err_t i2s_drv_write(i2s_drv_handle_t handle, const void* buffer, size_t size, size_t* bytes_written);

#ifdef __cplusplus
}
#endif

#endif /* I2S_DRV_H */
