/**
 * @file lora.h
 * @brief LoRa无线通信模块接口
 * 
 * 提供LoRa模块的初始化、发送、接收等功能。
 * 使用UART总线通信，通过event_bus发布无线数据事件。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef LORA_H
#define LORA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    LORA_OK = 0,
    LORA_ERR_NOT_INIT = -1,
    LORA_ERR_PARAM = -2,
    LORA_ERR_UART = -3,
    LORA_ERR_TIMEOUT = -4,
    LORA_ERR_BUSY = -5,
    LORA_ERR_NO_DATA = -6,
} lora_err_t;

/* ========== 类型定义 ========== */

/**
 * @brief LoRa配置参数
 */
typedef struct {
    uint8_t address_high;      /**< 地址高字节 */
    uint8_t address_low;       /**< 地址低字节 */
    uint8_t channel;           /**< 通信信道 (0-31) */
    uint8_t air_data_rate;     /**< 空中速率 */
    uint8_t transmit_power;    /**< 发射功率 */
} lora_config_t;

/**
 * @brief LoRa接收数据回调
 * 
 * @param data 数据指针
 * @param len 数据长度
 * @param rssi 信号强度
 * @param user_data 用户数据
 */
typedef void (*lora_rx_cb_t)(const uint8_t* data, size_t len, int8_t rssi, void* user_data);

/* ========== 公共 API ========== */

/**
 * @brief 初始化LoRa模块
 * 
 * 初始化UART和LoRa控制引脚。
 * 
 * @return LORA_OK 成功
 * @return LORA_ERR_UART UART初始化失败
 */
lora_err_t lora_init(void);

/**
 * @brief 反初始化LoRa模块
 * 
 * @return LORA_OK 成功
 */
lora_err_t lora_deinit(void);

/**
 * @brief 发送数据
 * 
 * @param data 数据缓冲区
 * @param size 数据大小（字节）
 * @return LORA_OK 成功
 * @return LORA_ERR_NOT_INIT 未初始化
 * @return LORA_ERR_BUSY 模块忙
 */
lora_err_t lora_send(const void* data, size_t size);

/**
 * @brief 接收数据（非阻塞）
 * 
 * @param buffer 数据缓冲区
 * @param size 缓冲区大小
 * @param bytes_read 实际读取的字节数
 * @return LORA_OK 成功
 * @return LORA_ERR_NO_DATA 无数据
 * @return LORA_ERR_NOT_INIT 未初始化
 */
lora_err_t lora_receive(void* buffer, size_t size, size_t* bytes_read);

/**
 * @brief 注册接收回调
 * 
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return LORA_OK 成功
 */
lora_err_t lora_register_rx_callback(lora_rx_cb_t callback, void* user_data);

/**
 * @brief 启动周期性接收任务
 * 
 * @param interval_ms 轮询间隔（毫秒）
 * @return LORA_OK 成功
 */
lora_err_t lora_start_receive_task(uint32_t interval_ms);

/**
 * @brief 停止周期性接收任务
 * 
 * @return LORA_OK 成功
 */
lora_err_t lora_stop_receive_task(void);

/**
 * @brief 检查LoRa是否已初始化
 * 
 * @return true 已初始化
 * @return false 未初始化
 */
bool lora_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* LORA_H */
