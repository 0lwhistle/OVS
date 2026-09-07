/**
 * @file vfs_block_dev.h
 * @brief VFS 块设备抽象接口
 * 
 * 定义块设备操作的回调函数表，由具体存储模块实现。
 * VFS 模块通过此接口与块设备交互，实现完全解耦。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef VFS_BLOCK_DEV_H
#define VFS_BLOCK_DEV_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    VFS_BD_OK = 0,
    VFS_BD_ERR_PARAM = -1,
    VFS_BD_ERR_IO = -2,
    VFS_BD_ERR_TIMEOUT = -3,
    VFS_BD_ERR_ERASE = -4,
    VFS_BD_ERR_NOT_FOUND = -5,
    VFS_BD_ERR_ALREADY_REG = -6,
    VFS_BD_ERR_FULL = -7,
} vfs_bd_err_t;

/* ========== 块设备操作回调 ========== */

/**
 * @brief 块设备操作函数表
 * 
 * 由具体存储模块实现，通过 vfs_block_dev_register() 注册到 VFS。
 */
typedef struct {
    /**
     * @brief 读取数据
     * @param priv 私有数据（由注册时传入）
     * @param buf 读取缓冲区
     * @param size 读取大小（字节）
     * @param offset 起始偏移（相对于设备起始）
     * @return VFS_BD_OK 成功，其他表示失败
     */
    vfs_bd_err_t (*read)(void* priv, void* buf, size_t size, size_t offset);
    
    /**
     * @brief 写入数据
     * @param priv 私有数据
     * @param buf 写入数据
     * @param size 写入大小（字节）
     * @param offset 起始偏移
     * @return VFS_BD_OK 成功
     */
    vfs_bd_err_t (*write)(void* priv, const void* buf, size_t size, size_t offset);
    
    /**
     * @brief 擦除块
     * @param priv 私有数据
     * @param offset 起始偏移（必须块对齐）
     * @param size 擦除大小（必须是 block_size 的整数倍）
     * @return VFS_BD_OK 成功
     */
    vfs_bd_err_t (*erase)(void* priv, size_t offset, size_t size);
    
    /**
     * @brief 获取设备总大小
     * @param priv 私有数据
     * @return 设备大小（字节）
     */
    size_t (*get_size)(void* priv);
    
} vfs_block_dev_ops_t;

/* ========== 块设备句柄 ========== */
typedef struct {
    const char* name;               /* 设备名称，如 "w25q128", "internal" */
    const vfs_block_dev_ops_t* ops; /* 操作回调表 */
    size_t block_size;              /* 擦除块大小（字节） */
    void* priv;                     /* 私有数据（由注册者管理） */
} vfs_block_dev_t;

/* ========== 注册/注销 API ========== */

/**
 * @brief 注册块设备
 * @param dev 块设备描述（name, ops, block_size 必须填充）
 * @return VFS_BD_OK 成功
 */
vfs_bd_err_t vfs_block_dev_register(const vfs_block_dev_t* dev);

/**
 * @brief 注销块设备
 * @param name 设备名称
 * @return VFS_BD_OK 成功
 */
vfs_bd_err_t vfs_block_dev_unregister(const char* name);

/**
 * @brief 获取块设备
 * @param name 设备名称
 * @return 块设备指针，NULL 表示未找到
 */
const vfs_block_dev_t* vfs_block_dev_get(const char* name);

/**
 * @brief 获取已注册块设备数量
 * @return 数量
 */
int vfs_block_dev_get_count(void);

/**
 * @brief 打印所有已注册块设备
 */
void vfs_block_dev_print_all(void);

#ifdef __cplusplus
}
#endif

#endif /* VFS_BLOCK_DEV_H */

/* ========== 内部 API（供 VFS 模块内部使用） ========== */

/**
 * @brief 初始化块设备表（内部使用）
 */
void vfs_block_dev_init(void);

/**
 * @brief 反初始化块设备表（内部使用）
 */
void vfs_block_dev_deinit(void);
