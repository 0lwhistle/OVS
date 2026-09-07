/**
 * @file internal_flash_vfs.c
 * @brief 内部 Flash VFS 回调实现
 * 
 * 将 ESP32 内部 Flash 适配为 VFS 块设备接口。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "internal_flash_vfs.h"
#include "ovs_vfs_block_dev.h"
#include "logger.h"
#include "esp_partition.h"

#include <stddef.h>

static const char* TAG = "[INT_FLASH_VFS]";

/* ========== 内部变量 ========== */

/** 分区句柄 */
static const esp_partition_t* s_partition = NULL;

/* ========== 回调实现 ========== */

static vfs_bd_err_t internal_flash_read(void* priv, void* buf, size_t size, size_t offset) {
    if (!s_partition) {
        return VFS_BD_ERR_IO;
    }
    
    esp_err_t err = esp_partition_read(s_partition, offset, buf, size);
    if (err != ESP_OK) {
        LOGE(TAG, "Read failed: offset=%u, size=%u, err=%s", offset, size, esp_err_to_name(err));
        return VFS_BD_ERR_IO;
    }
    return VFS_BD_OK;
}

static vfs_bd_err_t internal_flash_write(void* priv, const void* buf, size_t size, size_t offset) {
    if (!s_partition) {
        return VFS_BD_ERR_IO;
    }
    
    esp_err_t err = esp_partition_write(s_partition, offset, buf, size);
    if (err != ESP_OK) {
        LOGE(TAG, "Write failed: offset=%u, size=%u, err=%s", offset, size, esp_err_to_name(err));
        return VFS_BD_ERR_IO;
    }
    return VFS_BD_OK;
}

static vfs_bd_err_t internal_flash_erase(void* priv, size_t offset, size_t size) {
    if (!s_partition) {
        return VFS_BD_ERR_IO;
    }
    
    /* 检查对齐 */
    if (offset % 4096 != 0 || size % 4096 != 0) {
        LOGE(TAG, "Erase not aligned: offset=%u, size=%u", offset, size);
        return VFS_BD_ERR_PARAM;
    }
    
    esp_err_t err = esp_partition_erase_range(s_partition, offset, size);
    if (err != ESP_OK) {
        LOGE(TAG, "Erase failed: offset=%u, size=%u, err=%s", offset, size, esp_err_to_name(err));
        return VFS_BD_ERR_ERASE;
    }
    return VFS_BD_OK;
}

static size_t internal_flash_get_size(void* priv) {
    if (!s_partition) {
        return 0;
    }
    return s_partition->size;
}

/* ========== 回调函数表 ========== */
static const vfs_block_dev_ops_t s_internal_flash_ops = {
    .read = internal_flash_read,
    .write = internal_flash_write,
    .erase = internal_flash_erase,
    .get_size = internal_flash_get_size,
};

/* ========== 公共 API ========== */

void internal_flash_register_vfs(const char* partition_label) {
    if (!partition_label) {
        LOGE(TAG, "Partition label is NULL");
        return;
    }
    
    /* 查找分区 */
    s_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 
        ESP_PARTITION_SUBTYPE_ANY, 
        partition_label);
    
    if (!s_partition) {
        LOGE(TAG, "Partition '%s' not found", partition_label);
        return;
    }
    
    LOGI(TAG, "Found partition '%s': addr=0x%06lx, size=%luKB", 
         partition_label, s_partition->address, s_partition->size / 1024);
    
    /* 注册到 VFS */
    vfs_block_dev_t dev = {
        .name = "internal",
        .ops = &s_internal_flash_ops,
        .block_size = 4096,
        .priv = NULL,
    };
    
    vfs_bd_err_t err = vfs_block_dev_register(&dev);
    if (err != VFS_BD_OK) {
        LOGE(TAG, "Failed to register to VFS: %d", err);
        return;
    }
    
    LOGI(TAG, "Internal flash registered to VFS");
}
