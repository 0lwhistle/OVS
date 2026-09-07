/**
 * @file w25q128_vfs.h
 * @brief W25Q128 VFS 注册接口
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef W25Q128_VFS_H
#define W25Q128_VFS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册 W25Q128 到 VFS 块设备表
 * 
 * 必须在 w25q128_init() 之后调用。
 */
void w25q128_register_vfs(void);

#ifdef __cplusplus
}
#endif

#endif /* W25Q128_VFS_H */
