/**
 * @file spi_drv.h
 * @brief SPI驱动接口
 * 
 * 提供SPI总线初始化、配置和数据传输功能。
 * 硬件参数从设备树读取，不在代码中硬编码。
 * 支持多个设备共享同一SPI总线。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef SPI_DRV_H
#define SPI_DRV_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dtree.h"

/* 包含ESP-IDF SPI头文件以获取spi_device_handle_t */
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    SPI_DRV_OK = 0,
    SPI_DRV_ERR_NOT_INIT = -1,
    SPI_DRV_ERR_CONFIG = -2,
    SPI_DRV_ERR_PARAM = -3,
    SPI_DRV_ERR_HW = -4,
} spi_drv_err_t;

/* ========== 类型定义 ========== */

/**
 * @brief SPI总线配置结构
 * 
 * 从设备树读取，包含总线引脚和时钟配置。
 */
typedef struct spi_drv_config {
    int32_t sclk_pin;        /**< SPI时钟引脚 */
    int32_t miso_pin;        /**< MISO引脚 */
    int32_t mosi_pin;        /**< MOSI引脚 */
    int32_t max_freq_mhz;    /**< 最大频率(MHz) */
    int32_t mode;            /**< SPI模式 (0-3) */
} spi_drv_config_t;

/** SPI驱动句柄类型（不透明指针） */
typedef struct spi_drv_handle* spi_drv_handle_t;

/* spi_device_handle_t 已在 spi_master.h 中定义 */

/* ========== 公共 API ========== */

/**
 * @brief 从设备树加载SPI配置
 * 
 * @param config 输出参数，存储配置信息
 * @return spi_drv_err_t 错误码
 */
spi_drv_err_t spi_drv_load_config(spi_drv_config_t* config);

/**
 * @brief 初始化SPI总线驱动
 * 
 * 从设备树读取配置，初始化SPI主机总线。
 * 
 * @param config 配置信息
 * @param handle 输出参数，存储驱动句柄
 * @return spi_drv_err_t 错误码
 */
spi_drv_err_t spi_drv_init(const spi_drv_config_t* config, spi_drv_handle_t* handle);

/**
 * @brief 反初始化SPI总线驱动
 * 
 * @param handle 驱动句柄
 * @return spi_drv_err_t 错误码
 */
spi_drv_err_t spi_drv_deinit(spi_drv_handle_t handle);

/**
 * @brief 添加SPI设备到总线
 * 
 * @param handle 驱动句柄
 * @param cs_pin 片选引脚
 * @param clock_speed_hz 时钟频率(Hz)
 * @param mode SPI模式 (0-3)
 * @param dev_handle 输出参数，存储设备句柄
 * @return spi_drv_err_t 错误码
 */
spi_drv_err_t spi_drv_add_device(spi_drv_handle_t handle, 
                                  int cs_pin, 
                                  int clock_speed_hz, 
                                  int mode,
                                  spi_device_handle_t* dev_handle);

/**
 * @brief SPI数据传输（同时发送和接收）
 * 
 * @param handle 驱动句柄
 * @param dev_handle 设备句柄
 * @param tx_data 发送数据缓冲区
 * @param rx_data 接收数据缓冲区
 * @param size 数据大小（字节）
 * @return spi_drv_err_t 错误码
 */
spi_drv_err_t spi_drv_transfer(spi_drv_handle_t handle, 
                                spi_device_handle_t dev_handle,
                                const void* tx_data, 
                                void* rx_data, 
                                size_t size);

/**
 * @brief SPI写数据
 * 
 * @param handle 驱动句柄
 * @param dev_handle 设备句柄
 * @param data 数据缓冲区
 * @param size 数据大小（字节）
 * @return spi_drv_err_t 错误码
 */
spi_drv_err_t spi_drv_write(spi_drv_handle_t handle, 
                             spi_device_handle_t dev_handle,
                             const void* data, 
                             size_t size);

/**
 * @brief SPI读数据
 * 
 * @param handle 驱动句柄
 * @param dev_handle 设备句柄
 * @param buffer 接收缓冲区
 * @param size 缓冲区大小（字节）
 * @return spi_drv_err_t 错误码
 */
spi_drv_err_t spi_drv_read(spi_drv_handle_t handle, 
                            spi_device_handle_t dev_handle,
                            void* buffer, 
                            size_t size);

/**
 * @brief 发送命令+数据（用于LCD等设备）
 * 
 * 先发送命令字节（DC低），再发送数据（DC高）。
 * 
 * @param handle 驱动句柄
 * @param dev_handle 设备句柄
 * @param dc_pin DC引脚 (-1表示不使用)
 * @param cmd 命令字节
 * @param data 数据缓冲区
 * @param data_len 数据长度
 * @return spi_drv_err_t 错误码
 */
spi_drv_err_t spi_drv_write_cmd_data(spi_drv_handle_t handle,
                                       spi_device_handle_t dev_handle,
                                       int dc_pin,
                                       uint8_t cmd,
                                       const void* data,
                                       size_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* SPI_DRV_H */
