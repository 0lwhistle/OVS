/**
 * @file i2c_drv.h
 * @brief I2C驱动接口
 * 
 * 提供I2C总线初始化、配置和数据传输功能。
 * 硬件参数从设备树读取。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef I2C_DRV_H
#define I2C_DRV_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dtree.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    I2C_DRV_OK = 0,
    I2C_DRV_ERR_NOT_INIT = -1,
    I2C_DRV_ERR_CONFIG = -2,
    I2C_DRV_ERR_PARAM = -3,
    I2C_DRV_ERR_HW = -4,
    I2C_DRV_ERR_TIMEOUT = -5,
} i2c_drv_err_t;

/* ========== 类型定义 ========== */
typedef struct i2c_drv_config {
    int32_t port;               /**< I2C 控制器编号：设备树 "i2c0"→0、"i2c1"→1 */
    int32_t sda_pin;
    int32_t scl_pin;
    int32_t freq_hz;
    bool pullup;
    int32_t pullup_resistor_ohm;
} i2c_drv_config_t;

typedef struct i2c_drv_handle* i2c_drv_handle_t;

/* ========== 公共 API ========== */

/**
 * @brief 从设备树总线节点加载I2C配置
 *
 * @param bus_node 总线节点（节点名形如 "i2c0"，设备节点的父节点）
 * @param config 输出参数，存储配置信息
 * @return i2c_drv_err_t 错误码
 */
i2c_drv_err_t i2c_drv_load_config(dtree_node_t* bus_node, i2c_drv_config_t* config);

/**
 * @brief 初始化I2C驱动
 *
 * Linux 式共享计数模型：同一条 I2C 总线（SDA/SCL 一致）只初始化一次，
 * 多个模块（ath30、CST816S 等）获取同一个句柄并增加引用计数。
 * 配置不一致时返回 I2C_DRV_ERR_CONFIG。
 * 
 * @param config 配置信息
 * @param handle 输出参数，存储共享的总线句柄
 * @return i2c_drv_err_t 错误码
 */
i2c_drv_err_t i2c_drv_init(const i2c_drv_config_t* config, i2c_drv_handle_t* handle);

/**
 * @brief 反初始化I2C驱动
 *
 * 只释放本调用方的一个引用；引用计数归零时才删除总线与设备链表。
 * 
 * @param handle 驱动句柄
 * @return i2c_drv_err_t 错误码
 */
i2c_drv_err_t i2c_drv_deinit(i2c_drv_handle_t handle);

/**
 * @brief 向I2C设备写入数据
 * 
 * @param handle 驱动句柄
 * @param device_addr 设备地址
 * @param data 数据缓冲区
 * @param size 数据大小
 * @return i2c_drv_err_t 错误码
 */
i2c_drv_err_t i2c_drv_write(i2c_drv_handle_t handle, uint8_t device_addr, const void* data, size_t size);

/**
 * @brief 从I2C设备读取数据
 * 
 * @param handle 驱动句柄
 * @param device_addr 设备地址
 * @param buffer 数据缓冲区
 * @param size 缓冲区大小
 * @param bytes_read 实际读取的字节数
 * @return i2c_drv_err_t 错误码
 */
i2c_drv_err_t i2c_drv_read(i2c_drv_handle_t handle, uint8_t device_addr, void* buffer, size_t size, size_t* bytes_read);

/**
 * @brief 向I2C设备的寄存器写入数据
 * 
 * @param handle 驱动句柄
 * @param device_addr 设备地址
 * @param reg_addr 寄存器地址
 * @param data 数据缓冲区
 * @param size 数据大小
 * @return i2c_drv_err_t 错误码
 */
i2c_drv_err_t i2c_drv_write_reg(i2c_drv_handle_t handle, uint8_t device_addr, uint8_t reg_addr, const void* data, size_t size);

/**
 * @brief 从I2C设备的寄存器读取数据
 * 
 * @param handle 驱动句柄
 * @param device_addr 设备地址
 * @param reg_addr 寄存器地址
 * @param buffer 数据缓冲区
 * @param size 缓冲区大小
 * @param bytes_read 实际读取的字节数
 * @return i2c_drv_err_t 错误码
 */
i2c_drv_err_t i2c_drv_read_reg(i2c_drv_handle_t handle, uint8_t device_addr, uint8_t reg_addr, void* buffer, size_t size, size_t* bytes_read);

#ifdef __cplusplus
}
#endif

#endif /* I2C_DRV_H */
