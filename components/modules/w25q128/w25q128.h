/**
 * @file w25q128.h
 * @brief W25Q128 NOR Flash存储模块接口
 * 
 * 提供W25Q128 Flash的初始化、读写、擦除等功能。
 * 使用SPI总线通信，通过event_bus发布存储状态事件。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef W25Q128_H
#define W25Q128_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    W25Q128_OK = 0,
    W25Q128_ERR_NOT_INIT = -1,
    W25Q128_ERR_PARAM = -2,
    W25Q128_ERR_SPI = -3,
    W25Q128_ERR_TIMEOUT = -4,
    W25Q128_ERR_BUSY = -5,
    W25Q128_ERR_VERIFY = -6,
} w25q128_err_t;

/* ========== 类型定义 ========== */

/**
 * @brief W25Q128信息结构
 */
typedef struct {
    uint8_t manufacturer_id;   /**< 制造商ID */
    uint8_t memory_type;       /**< 存储类型 */
    uint8_t capacity;          /**< 容量ID */
    uint32_t total_size;       /**< 总容量（字节） */
    uint32_t sector_size;      /**< 扇区大小（字节） */
    uint32_t page_size;        /**< 页大小（字节） */
    uint32_t sector_count;     /**< 扇区数量 */
} w25q128_info_t;

/* ========== 公共 API ========== */

/**
 * @brief 初始化W25Q128 Flash
 * 
 * 初始化SPI总线和W25Q128设备。
 * 
 * @return W25Q128_OK 成功
 * @return W25Q128_ERR_SPI SPI初始化失败
 */
w25q128_err_t w25q128_init(void);

/**
 * @brief 反初始化W25Q128 Flash
 * 
 * @return W25Q128_OK 成功
 */
w25q128_err_t w25q128_deinit(void);

/**
 * @brief 获取W25Q128信息
 * 
 * @param info 输出参数，存储Flash信息
 * @return W25Q128_OK 成功
 * @return W25Q128_ERR_NOT_INIT 未初始化
 */
w25q128_err_t w25q128_get_info(w25q128_info_t* info);

/**
 * @brief 读取数据
 * 
 * @param addr 起始地址
 * @param buffer 数据缓冲区
 * @param size 数据大小（字节）
 * @return W25Q128_OK 成功
 * @return W25Q128_ERR_NOT_INIT 未初始化
 * @return W25Q128_ERR_PARAM 参数错误
 */
w25q128_err_t w25q128_read(uint32_t addr, void* buffer, size_t size);

/**
 * @brief 写入数据（自动擦除）
 * 
 * @param addr 起始地址（必须扇区对齐）
 * @param data 数据缓冲区
 * @param size 数据大小（字节）
 * @return W25Q128_OK 成功
 * @return W25Q128_ERR_NOT_INIT 未初始化
 * @return W25Q128_ERR_PARAM 参数错误
 * @return W25Q128_ERR_VERIFY 数据校验失败
 */
w25q128_err_t w25q128_write(uint32_t addr, const void* data, size_t size);

/**
 * @brief 擦除扇区
 * 
 * @param sector_index 扇区索引
 * @return W25Q128_OK 成功
 * @return W25Q128_ERR_NOT_INIT 未初始化
 * @return W25Q128_ERR_TIMEOUT 超时
 */
w25q128_err_t w25q128_erase_sector(uint32_t sector_index);

/**
 * @brief 擦除整片Flash
 * 
 * @return W25Q128_OK 成功
 * @return W25Q128_ERR_NOT_INIT 未初始化
 * @return W25Q128_ERR_TIMEOUT 超时
 */
w25q128_err_t w25q128_erase_chip(void);

/**
 * @brief 检查W25Q128是否已初始化
 * 
 * @return true 已初始化
 * @return false 未初始化
 */
bool w25q128_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* W25Q128_H */
