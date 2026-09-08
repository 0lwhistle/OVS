/**
 * @file spi_drv.h
 * @brief SPI驱动接口（支持DMA异步传输）
 * 
 * 功能特性：
 * - 支持DMA异步传输，减少CPU占用
 * - 支持多个设备共享同一SPI总线（如Flash + LCD）
 * - 提供同步和异步两种API
 * - 硬件参数从设备树读取
 * 
 * 传输模式：
 * - 同步模式：阻塞等待传输完成，适合小数据量
 * - 异步模式：DMA后台传输，适合大块数据（LCD帧、音频）
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
    SPI_DRV_ERR_TIMEOUT = -5,
    SPI_DRV_ERR_DMA = -6,
} spi_drv_err_t;

/* ========== 传输模式 ========== */
typedef enum {
    SPI_XFER_MODE_POLLING = 0,  /**< 轮询模式（同步，CPU等待） */
    SPI_XFER_MODE_DMA_SYNC,    /**< DMA同步模式（DMA传输，阻塞等待） */
    SPI_XFER_MODE_DMA_ASYNC,   /**< DMA异步模式（DMA传输，非阻塞） */
} spi_xfer_mode_t;

/* ========== 配置结构 ========== */

/**
 * @brief SPI总线配置（从设备树读取）
 */
typedef struct spi_drv_config {
    int32_t host;                   /**< 控制器编号：设备树 "spi2"→2、"spi3"→3 */
    int32_t sclk_pin;
    int32_t miso_pin;
    int32_t mosi_pin;
    int32_t max_freq_mhz;
    int32_t mode;
} spi_drv_config_t;

/**
 * @brief SPI设备配置（添加设备时传入）
 */
typedef struct spi_dev_config {
    int cs_pin;                  /**< 片选引脚 */
    int clock_speed_hz;          /**< 时钟频率 */
    int mode;                    /**< SPI模式 */
    spi_xfer_mode_t xfer_mode;  /**< 默认传输模式 */
    int max_transfer_sz;         /**< 单次最大传输大小 */
} spi_dev_config_t;

/** SPI驱动句柄（不透明指针） */
typedef struct spi_drv_handle* spi_drv_handle_t;

/** DMA传输完成回调 */
typedef void (*spi_dma_callback_t)(void* arg);

/* ========== 公共 API ========== */

/* ---------- 初始化/反初始化 ---------- */

spi_drv_err_t spi_drv_load_config(dtree_node_t* bus_node, spi_drv_config_t* config);

/**
 * @brief 获取共享的 SPI 总线句柄（Linux 式引用计数）
 *
 * 同一 SPI 总线只初始化一次；多个设备模块调用本接口会得到同一个句柄，
 * 引用计数 +1。若请求配置与已存在总线不一致，返回 SPI_DRV_ERR_CONFIG。
 */
spi_drv_err_t spi_drv_init(const spi_drv_config_t* config, spi_drv_handle_t* handle);

/**
 * @brief 释放一个 SPI 总线引用
 *
 * 只递减引用计数；归零时才真正销毁总线。释放设备前应先调用
 * spi_drv_remove_device()。
 */
spi_drv_err_t spi_drv_deinit(spi_drv_handle_t handle);

/* ---------- 设备管理 ---------- */

spi_drv_err_t spi_drv_add_device(spi_drv_handle_t handle, 
                                  const spi_dev_config_t* config,
                                  spi_device_handle_t* dev_handle);

spi_drv_err_t spi_drv_remove_device(spi_drv_handle_t handle, 
                                     spi_device_handle_t dev_handle);

/* ---------- 同步传输（阻塞） ---------- */

/**
 * @brief 全双工传输（同时发送和接收）
 */
spi_drv_err_t spi_drv_transfer(spi_drv_handle_t handle, 
                                spi_device_handle_t dev_handle,
                                const void* tx_data, 
                                void* rx_data, 
                                size_t size);

spi_drv_err_t spi_drv_write(spi_drv_handle_t handle, 
                             spi_device_handle_t dev_handle,
                             const void* data, 
                             size_t size);

spi_drv_err_t spi_drv_read(spi_drv_handle_t handle, 
                            spi_device_handle_t dev_handle,
                            void* buffer, 
                            size_t size);

/* ---------- DMA 异步传输（非阻塞） ---------- */

/**
 * @brief DMA异步写入
 * 
 * 启动DMA传输后立即返回，通过回调或等待获取完成状态。
 * 
 * @param handle 驱动句柄
 * @param dev_handle 设备句柄
 * @param data 数据缓冲区（必须DMA安全，或内部会复制）
 * @param size 数据大小
 * @param callback 完成回调（可选，NULL则不回调）
 * @param cb_arg 回调参数
 * @return spi_drv_err_t 
 */
spi_drv_err_t spi_drv_write_async(spi_drv_handle_t handle, 
                                    spi_device_handle_t dev_handle,
                                    const void* data, 
                                    size_t size,
                                    spi_dma_callback_t callback,
                                    void* cb_arg);

/**
 * @brief 等待DMA传输完成
 * 
 * @param handle 驱动句柄
 * @param dev_handle 设备句柄
 * @param timeout_ms 超时时间（毫秒），-1表示永久等待
 * @return spi_drv_err_t 
 */
spi_drv_err_t spi_drv_wait_async(spi_drv_handle_t handle, 
                                   spi_device_handle_t dev_handle,
                                   int timeout_ms);

/* ---------- 特殊传输（LCD等） ---------- */

/**
 * @brief 发送命令+数据（LCD常用）
 * 
 * @param dc_pin DC引脚（-1表示不使用）
 * @param cmd 命令字节
 * @param data 数据（NULL表示只发命令）
 * @param data_len 数据长度
 */
spi_drv_err_t spi_drv_write_cmd_data(spi_drv_handle_t handle,
                                       spi_device_handle_t dev_handle,
                                       int dc_pin,
                                       uint8_t cmd,
                                       const void* data,
                                       size_t data_len);

/**
 * @brief DMA异步发送命令+数据
 */
spi_drv_err_t spi_drv_write_cmd_data_async(spi_drv_handle_t handle,
                                             spi_device_handle_t dev_handle,
                                             int dc_pin,
                                             uint8_t cmd,
                                             const void* data,
                                             size_t data_len,
                                             spi_dma_callback_t callback,
                                             void* cb_arg);

#ifdef __cplusplus
}
#endif

#endif /* SPI_DRV_H */
