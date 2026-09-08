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
    int32_t port;           /**< UART 控制器编号：设备树 "uart0"→0、"uart1"→1、"uart2"→2 */
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
 * @brief 从设备树总线节点加载UART配置
 *
 * @param bus_node 总线节点（节点名形如 "uart1"，设备节点的父节点）
 * @param config 输出参数，存储配置信息
 * @return uart_drv_err_t 错误码
 */
uart_drv_err_t uart_drv_load_config(dtree_node_t* bus_node, uart_drv_config_t* config);

/**
 * @brief 初始化UART驱动
 * 
 * Linux 式共享计数模型：相同 UART 端口/引脚只初始化一次，
 * 多个模块获取同一个句柄并增加引用计数。
 * 
 * @param config 配置信息
 * @param handle 输出参数，存储共享的驱动句柄
 * @return uart_drv_err_t 错误码
 */
uart_drv_err_t uart_drv_init(const uart_drv_config_t* config, uart_drv_handle_t* handle);

/**
 * @brief 反初始化UART驱动
 *
 * 只释放一个引用；引用计数归零时才真正删除 UART 驱动。
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
