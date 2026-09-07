/**
 * @file vfs_block_dev.c
 * @brief VFS 块设备注册表实现
 * 
 * 管理块设备的注册、查找、注销。
 * 使用静态数组存储，避免动态内存分配。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "ovs_vfs_block_dev.h"
#include "logger.h"

#include <string.h>

static const char* TAG = "[VFS_BD]";

/* ========== 内部常量 ========== */
#define MAX_BLOCK_DEVS  8   /* 最大块设备数量 */

/* ========== 内部变量 ========== */

/** 块设备表 */
static vfs_block_dev_t s_dev_table[MAX_BLOCK_DEVS];

/** 已注册数量 */
static int s_dev_count = 0;

/** 初始化标志 */
static bool s_initialized = false;

/* ========== 内部函数 ========== */

/**
 * @brief 查找设备索引
 * @param name 设备名称
 * @return 索引，-1 表示未找到
 */
static int find_dev_index(const char* name) {
    if (!name) {
        return -1;
    }
    
    for (int i = 0; i < s_dev_count; i++) {
        if (s_dev_table[i].name && strcmp(s_dev_table[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* ========== 公共 API 实现 ========== */

vfs_bd_err_t vfs_block_dev_register(const vfs_block_dev_t* dev) {
    if (!s_initialized) {
        LOGE(TAG, "Block dev table not initialized");
        return VFS_BD_ERR_PARAM;
    }
    
    if (!dev || !dev->name || !dev->ops) {
        LOGE(TAG, "Invalid parameters");
        return VFS_BD_ERR_PARAM;
    }
    
    /* 检查是否已注册 */
    if (find_dev_index(dev->name) >= 0) {
        LOGW(TAG, "Device '%s' already registered", dev->name);
        return VFS_BD_ERR_ALREADY_REG;
    }
    
    /* 检查是否已满 */
    if (s_dev_count >= MAX_BLOCK_DEVS) {
        LOGE(TAG, "Block device table full (%d/%d)", s_dev_count, MAX_BLOCK_DEVS);
        return VFS_BD_ERR_FULL;
    }
    
    /* 注册设备 */
    memcpy(&s_dev_table[s_dev_count], dev, sizeof(vfs_block_dev_t));
    s_dev_count++;
    
    LOGI(TAG, "Registered block device: '%s' (block_size=%u)", 
         dev->name, dev->block_size);
    
    return VFS_BD_OK;
}

vfs_bd_err_t vfs_block_dev_unregister(const char* name) {
    if (!s_initialized || !name) {
        return VFS_BD_ERR_PARAM;
    }
    
    int idx = find_dev_index(name);
    if (idx < 0) {
        LOGW(TAG, "Device '%s' not found", name);
        return VFS_BD_ERR_NOT_FOUND;
    }
    
    /* 移动后面的设备 */
    for (int i = idx; i < s_dev_count - 1; i++) {
        memcpy(&s_dev_table[i], &s_dev_table[i + 1], sizeof(vfs_block_dev_t));
    }
    s_dev_count--;
    
    LOGI(TAG, "Unregistered block device: '%s'", name);
    return VFS_BD_OK;
}

const vfs_block_dev_t* vfs_block_dev_get(const char* name) {
    if (!s_initialized || !name) {
        return NULL;
    }
    
    int idx = find_dev_index(name);
    if (idx < 0) {
        return NULL;
    }
    
    return &s_dev_table[idx];
}

int vfs_block_dev_get_count(void) {
    return s_dev_count;
}

void vfs_block_dev_print_all(void) {
    LOGI(TAG, "=== Block Devices (%d/%d) ===", s_dev_count, MAX_BLOCK_DEVS);
    
    if (s_dev_count == 0) {
        LOGI(TAG, "  (none)");
        return;
    }
    
    for (int i = 0; i < s_dev_count; i++) {
        const vfs_block_dev_t* dev = &s_dev_table[i];
        size_t size_mb = dev->ops->get_size(dev->priv) / (1024 * 1024);
        LOGI(TAG, "  [%d] %-12s  block=%4u  size=%uMB", 
             i, dev->name, dev->block_size, size_mb);
    }
}

/* ========== 初始化/反初始化 ========== */

void vfs_block_dev_init(void) {
    memset(s_dev_table, 0, sizeof(s_dev_table));
    s_dev_count = 0;
    s_initialized = true;
    LOGI(TAG, "Block device table initialized");
}

void vfs_block_dev_deinit(void) {
    s_dev_count = 0;
    s_initialized = false;
    LOGI(TAG, "Block device table deinitialized");
}
