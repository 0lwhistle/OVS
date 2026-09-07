/**
 * @file internal_flash_vfs.h
 * @brief 内部 Flash VFS 注册接口
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef INTERNAL_FLASH_VFS_H
#define INTERNAL_FLASH_VFS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册内部 Flash 到 VFS 块设备表
 * 
 * @param partition_label 分区标签，如 "littlefs"
 */
void internal_flash_register_vfs(const char* partition_label);

#ifdef __cplusplus
}
#endif

#endif /* INTERNAL_FLASH_VFS_H */
