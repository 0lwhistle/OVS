/**
 * @file vfs.h
 * @brief VFS 虚拟文件系统管理器
 * 
 * 负责路径路由和挂载管理，通过块设备回调接口与存储交互。
 * 不依赖任何具体存储实现，完全解耦。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef VFS_H
#define VFS_H

#include "ovs_vfs_block_dev.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    VFS_OK = 0,
    VFS_ERR_NOT_INIT = -1,
    VFS_ERR_PARAM = -2,
    VFS_ERR_MOUNT = -3,
    VFS_ERR_NO_DEV = -4,
    VFS_ERR_IO = -5,
    VFS_ERR_NO_MEM = -6,
    VFS_ERR_ALREADY_MOUNTED = -7,
    VFS_ERR_NOT_MOUNTED = -8,
    VFS_ERR_FULL = -9,
} vfs_err_t;

/* ========== 挂载配置 ========== */
typedef struct {
    const char* virtual_path;       /* 虚拟路径，如 "/audio" */
    const char* device_name;        /* 块设备名称，如 "w25q128" */
    size_t offset;                  /* 分区内偏移（字节） */
    size_t size;                    /* 分区大小（字节），0 表示使用设备全部 */
    bool format_if_fail;            /* 挂载失败是否格式化 */
    bool read_only;                 /* 是否只读 */
} vfs_mount_cfg_t;

/* ========== 挂载信息 ========== */
typedef struct {
    char virtual_path[32];          /* 虚拟路径 */
    char device_name[16];           /* 块设备名称 */
    size_t offset;                  /* 偏移 */
    size_t size;                    /* 大小 */
    bool mounted;                   /* 是否已挂载 */
} vfs_mount_info_t;

/* ========== 公共 API ========== */

/**
 * @brief 初始化 VFS 系统
 * @return VFS_OK 成功
 */
vfs_err_t vfs_init(void);

/**
 * @brief 反初始化 VFS 系统
 * @return VFS_OK 成功
 */
vfs_err_t vfs_deinit(void);

/**
 * @brief 挂载文件系统
 * @param cfg 挂载配置
 * @return VFS_OK 成功
 */
vfs_err_t vfs_mount(const vfs_mount_cfg_t* cfg);

/**
 * @brief 卸载文件系统
 * @param virtual_path 虚拟路径
 * @return VFS_OK 成功
 */
vfs_err_t vfs_unmount(const char* virtual_path);

/**
 * @brief 检查路径是否已挂载
 * @param path 文件路径
 * @return true 已挂载
 */
bool vfs_is_mounted(const char* path);

/**
 * @brief 获取挂载点数量
 * @return 已挂载数量
 */
int vfs_get_mount_count(void);

/**
 * @brief 获取挂载信息
 * @param index 索引
 * @param info 输出信息
 * @return VFS_OK 成功
 */
vfs_err_t vfs_get_mount_info(int index, vfs_mount_info_t* info);

/**
 * @brief 打印 VFS 状态
 */
void vfs_print_status(void);

#ifdef __cplusplus
}
#endif

#endif /* VFS_H */

/* ========== LittleFS 挂载 API ========== */

/**
 * @brief 挂载 LittleFS 文件系统
 * @param virtual_path 虚拟路径
 * @param device_name 块设备名称
 * @param offset 分区偏移
 * @param size 分区大小，0 表示使用设备全部
 * @param format_if_fail 挂载失败是否格式化
 * @return VFS_OK 成功
 */
vfs_err_t vfs_mount_littlefs(const char* virtual_path, const char* device_name,
                              size_t offset, size_t size, bool format_if_fail);

/* ========== 内部 API（供 LittleFS 适配层使用） ========== */

/**
 * @brief 注册挂载点到挂载表（内部使用，由适配层调用）
 * @param virtual_path 虚拟路径
 * @param device_name 块设备名称
 * @param offset 分区偏移
 * @param size 分区大小
 * @param blockdev_handle esp_blockdev 句柄（用于格式化等操作）
 * @return VFS_OK 成功
 */
vfs_err_t vfs_register_mount_point(const char* virtual_path, const char* device_name,
                                    size_t offset, size_t size, void* blockdev_handle);

/**
 * @brief 释放块设备适配器（内部使用，由卸载流程调用）
 * @param handle esp_blockdev 句柄
 */
void vfs_release_blockdev_adapter(void* handle);

/* ========== 便捷 API ========== */

/**
 * @brief 卸载所有已挂载的文件系统
 * @return VFS_OK 成功
 */
vfs_err_t vfs_unmount_all(void);

/**
 * @brief 格式化指定挂载点
 * @param virtual_path 虚拟路径
 * @return VFS_OK 成功
 */
vfs_err_t vfs_format(const char* virtual_path);

/**
 * @brief 获取挂载点信息（通过路径）
 * @param path 文件路径
 * @param info 输出信息
 * @return VFS_OK 成功
 */
vfs_err_t vfs_get_mount_info_by_path(const char* path, vfs_mount_info_t* info);
