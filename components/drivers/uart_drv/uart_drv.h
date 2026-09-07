/**
 * @file uart_drv.h
 * @brief UART驱动接口
 * 
 * 提供UART串口初始化、配置和数据传输功能。
 * 硬件参数从设备树读取，不在代码中硬编码。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef UART_DRV_H
#define UART_DRV_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dtree.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    UART_DRV_OK = 0,
    UART_DRV_ERR_NOT_INIT = -1,
    UART_DRV_ERR_CONFIG = -2,
    UART_DRV_ERR_PARAM = -3,
    UART_DRV_ERR_HW = -4,
    UART_DRV_ERR_TIMEOUT = -5,
} uart_drv_err_t;

/* ========== 类型定义 ========== */

/**
 * @brief UART配置结构
 * 
 * 从设备树读取，包含引脚和通信参数。
 */
typedef struct uart_drv_config {
    int32_t tx_pin;          /**< TX引脚 */
    int32_t rx_pin;          /**< RX引脚 */
    int32_t baud_rate;       /**< 波特率 */
    int32_t data_bits;       /**< 数据位 (7或8) */
    int32_t stop_bits;       /**< 停止位 (1或2) */
    const char* parity;      /**< 奇偶校验 ("none", "even", "odd") */
} uart_drv_config_t;

/** UART驱动句柄类型（不透明指针） */
typedef struct uart_drv_handle* uart_drv_handle_t;

/* ========== 公共 API ========== */

/**
 * @brief 从设备树加载UART配置
 * 
 * @param path 设备树路径，如 "lora.uart" 或 "system.uart0"
 * @param config 输出参数，存储配置信息
 * @return uart_drv_err_t 错误码
 */
uart_drv_err_t uart_drv_load_config(const char* path, uart_drv_config_t* config);

/**
 * @brief 初始化UART驱动
 * 
 * 从设备树读取配置，初始化UART端口。
 * 
 * @param config 配置信息
 * @param handle 输出参数，存储驱动句柄
 * @return uart_drv_err_t 错误码
 */
uart_drv_err_t uart_drv_init(const uart_drv_config_t* config, uart_drv_handle_t* handle);

/**
 * @brief 反初始化UART驱动
 * 
 * @param handle 驱动句柄
 * @return uart_drv_err_t 错误码
 */
uart_drv_err_t uart_drv_deinit(uart_drv_handle_t handle);

/**
 * @brief 发送数据
 * 
 * @param handle 驱动句柄
 * @param data 数据缓冲区
 * @param size 数据大小（字节）
 * @return uart_drv_err_t 错误码
 */
uart_drv_err_t uart_drv_send(uart_drv_handle_t handle, const void* data, size_t size);

/**
 * @brief 接收数据
 * 
 * @param handle 驱动句柄
 * @param buffer 数据缓冲区
 * @param size 缓冲区大小（字节）
 * @param bytes_read 实际读取的字节数
 * @param timeout_ms 超时时间（毫秒）
 * @return uart_drv_err_t 错误码
 */
uart_drv_err_t uart_drv_receive(uart_drv_handle_t handle, void* buffer, size_t size, 
                                 size_t* bytes_read, uint32_t timeout_ms);

/**
 * @brief 清空UART缓冲区
 * 
 * @param handle 驱动句柄
 * @return uart_drv_err_t 错误码
 */
uart_drv_err_t uart_drv_flush(uart_drv_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* UART_DRV_H */
