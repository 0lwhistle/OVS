/**
 * @file w25q128_vfs.c
 * @brief W25Q128 VFS 回调实现
 * 
 * 将 W25Q128 驱动适配为 VFS 块设备接口。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "w25q128_vfs.h"
#include "w25q128.h"
#include "ovs_vfs_block_dev.h"
#include "logger.h"

#include <stddef.h>

static const char* TAG = "[W25Q128_VFS]";

/* ========== 回调实现 ========== */

static vfs_bd_err_t w25q128_vfs_read(void* priv, void* buf, size_t size, size_t offset) {
    w25q128_err_t err = w25q128_read(offset, buf, size);
    if (err != W25Q128_OK) {
        LOGE(TAG, "Read failed: offset=%u, size=%u, err=%d", offset, size, err);
        return VFS_BD_ERR_IO;
    }
    return VFS_BD_OK;
}

static vfs_bd_err_t w25q128_vfs_write(void* priv, const void* buf, size_t size, size_t offset) {
    w25q128_err_t err = w25q128_write(offset, buf, size);
    if (err != W25Q128_OK) {
        LOGE(TAG, "Write failed: offset=%u, size=%u, err=%d", offset, size, err);
        return VFS_BD_ERR_IO;
    }
    return VFS_BD_OK;
}

static vfs_bd_err_t w25q128_vfs_erase(void* priv, size_t offset, size_t size) {
    /* W25Q128 扇区大小 4KB */
    const size_t sector_size = 4096;
    
    /* 检查对齐 */
    if (offset % sector_size != 0 || size % sector_size != 0) {
        LOGE(TAG, "Erase not aligned: offset=%u, size=%u", offset, size);
        return VFS_BD_ERR_PARAM;
    }
    
    size_t start_sector = offset / sector_size;
    size_t sector_count = size / sector_size;
    
    for (size_t i = 0; i < sector_count; i++) {
        w25q128_err_t err = w25q128_erase_sector(start_sector + i);
        if (err != W25Q128_OK) {
            LOGE(TAG, "Erase failed: sector=%u, err=%d", start_sector + i, err);
            return VFS_BD_ERR_ERASE;
        }
    }
    
    return VFS_BD_OK;
}

static size_t w25q128_vfs_get_size(void* priv) {
    w25q128_info_t info;
    if (w25q128_get_info(&info) != W25Q128_OK) {
        return 0;
    }
    return info.total_size;
}

/* ========== 回调函数表 ========== */
static const vfs_block_dev_ops_t s_w25q128_ops = {
    .read = w25q128_vfs_read,
    .write = w25q128_vfs_write,
    .erase = w25q128_vfs_erase,
    .get_size = w25q128_vfs_get_size,
};

/* ========== 公共 API ========== */

void w25q128_register_vfs(void) {
    if (!w25q128_is_ready()) {
        LOGE(TAG, "W25Q128 not ready, cannot register to VFS");
        return;
    }
    
    vfs_block_dev_t dev = {
        .name = "w25q128",
        .ops = &s_w25q128_ops,
        .block_size = 4096,
        .priv = NULL,
    };
    
    vfs_bd_err_t err = vfs_block_dev_register(&dev);
    if (err != VFS_BD_OK) {
        LOGE(TAG, "Failed to register to VFS: %d", err);
        return;
    }
    
    LOGI(TAG, "W25Q128 registered to VFS");
}
