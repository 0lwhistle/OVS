/**
 * @file vfs.c
 * @brief VFS 虚拟文件系统管理器实现
 * 
 * 负责路径路由和挂载管理，通过块设备回调接口与存储交互。
 * 集成 esp_littlefs 实现真正的文件系统挂载。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "ovs_vfs.h"
#include "ovs_vfs_block_dev.h"
#include "logger.h"
#include "esp_littlefs.h"
#include "esp_vfs.h"

#include <string.h>
#include <stdlib.h>

static const char* TAG = "[VFS]";

/* ========== 内部常量 ========== */
#define MAX_MOUNT_POINTS    8   /* 最大挂载点数量 */
#define MAX_PATH_LEN        32  /* 最大路径长度 */

/* ========== 内部类型 ========== */

/** 挂载点描述 */
typedef struct {
    char virtual_path[MAX_PATH_LEN];    /* 虚拟路径 */
    char device_name[16];               /* 块设备名称 */
    size_t offset;                      /* 偏移 */
    size_t size;                        /* 大小 */
    bool mounted;                       /* 是否已挂载 */
    bool read_only;                     /* 是否只读 */
    void* blockdev_handle;              /* esp_blockdev 句柄（用于格式化） */
} mount_point_t;

/* ========== 内部变量 ========== */

/** 挂载点表 */
static mount_point_t s_mount_table[MAX_MOUNT_POINTS];

/** 挂载点数量 */
static int s_mount_count = 0;

/** 初始化标志 */
static bool s_initialized = false;

/* ========== 内部函数 ========== */

/**
 * @brief 查找挂载点索引
 */
static int find_mount_index(const char* path) {
    if (!path) return -1;
    
    for (int i = 0; i < s_mount_count; i++) {
        if (strcmp(s_mount_table[i].virtual_path, path) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 查找包含指定路径的挂载点
 */
static int find_mount_for_path(const char* path) {
    if (!path) return -1;
    
    int best_idx = -1;
    size_t best_len = 0;
    
    for (int i = 0; i < s_mount_count; i++) {
        if (!s_mount_table[i].mounted) continue;
        
        const char* mp = s_mount_table[i].virtual_path;
        size_t mp_len = strlen(mp);
        
        if (strncmp(path, mp, mp_len) == 0) {
            if (path[mp_len] == '\0' || path[mp_len] == '/') {
                if (mp_len > best_len) {
                    best_len = mp_len;
                    best_idx = i;
                }
            }
        }
    }
    
    return best_idx;
}

/* ========== 公共 API 实现 ========== */

vfs_err_t vfs_init(void) {
    if (s_initialized) {
        LOGW(TAG, "VFS already initialized");
        return VFS_OK;
    }
    
    LOGI(TAG, "Initializing VFS...");
    
    /* 初始化块设备表 */
    vfs_block_dev_init();
    
    /* 初始化挂载表 */
    memset(s_mount_table, 0, sizeof(s_mount_table));
    s_mount_count = 0;
    
    s_initialized = true;
    
    LOGI(TAG, "VFS initialized (max_mounts=%d)", MAX_MOUNT_POINTS);
    return VFS_OK;
}

vfs_err_t vfs_deinit(void) {
    if (!s_initialized) {
        return VFS_ERR_NOT_INIT;
    }
    
    LOGI(TAG, "Deinitializing VFS...");
    
    /* 卸载所有文件系统 */
    for (int i = s_mount_count - 1; i >= 0; i--) {
        if (s_mount_table[i].mounted) {
            vfs_unmount(s_mount_table[i].virtual_path);
        }
    }
    
    /* 反初始化块设备表 */
    vfs_block_dev_deinit();
    
    s_mount_count = 0;
    s_initialized = false;
    
    LOGI(TAG, "VFS deinitialized");
    return VFS_OK;
}

vfs_err_t vfs_mount(const vfs_mount_cfg_t* cfg) {
    if (!s_initialized) {
        LOGE(TAG, "VFS not initialized");
        return VFS_ERR_NOT_INIT;
    }
    
    if (!cfg || !cfg->virtual_path || !cfg->device_name) {
        LOGE(TAG, "Invalid parameters");
        return VFS_ERR_PARAM;
    }
    
    /* 检查是否已挂载 */
    if (find_mount_index(cfg->virtual_path) >= 0) {
        LOGW(TAG, "Path '%s' already mounted", cfg->virtual_path);
        return VFS_ERR_ALREADY_MOUNTED;
    }
    
    /* 检查挂载表是否已满 */
    if (s_mount_count >= MAX_MOUNT_POINTS) {
        LOGE(TAG, "Mount table full (%d/%d)", s_mount_count, MAX_MOUNT_POINTS);
        return VFS_ERR_FULL;
    }
    
    /* 查找块设备 */
    const vfs_block_dev_t* dev = vfs_block_dev_get(cfg->device_name);
    if (!dev) {
        LOGE(TAG, "Block device '%s' not found", cfg->device_name);
        return VFS_ERR_NO_DEV;
    }
    
    /* 计算实际大小 */
    size_t actual_size = cfg->size;
    if (actual_size == 0) {
        actual_size = dev->ops->get_size(dev->priv) - cfg->offset;
    }
    
    LOGI(TAG, "Mounting '%s' -> '%s' (offset=%u, size=%u)", 
         cfg->device_name, cfg->virtual_path, cfg->offset, actual_size);
    
    /* 记录挂载点 */
    mount_point_t* mp = &s_mount_table[s_mount_count];
    strncpy(mp->virtual_path, cfg->virtual_path, MAX_PATH_LEN - 1);
    strncpy(mp->device_name, cfg->device_name, 15);
    mp->offset = cfg->offset;
    mp->size = actual_size;
    mp->mounted = true;
    mp->read_only = cfg->read_only;
    s_mount_count++;
    
    LOGI(TAG, "Mounted successfully: %s (LittleFS integration pending)", cfg->virtual_path);
    return VFS_OK;
}

vfs_err_t vfs_register_mount_point(const char* virtual_path, const char* device_name,
                                    size_t offset, size_t size, void* blockdev_handle) {
    if (!s_initialized) {
        return VFS_ERR_NOT_INIT;
    }
    
    if (!virtual_path || !device_name) {
        return VFS_ERR_PARAM;
    }
    
    /* 检查是否已挂载 */
    if (find_mount_index(virtual_path) >= 0) {
        LOGW(TAG, "Path '%s' already registered", virtual_path);
        return VFS_ERR_ALREADY_MOUNTED;
    }
    
    /* 检查挂载表是否已满 */
    if (s_mount_count >= MAX_MOUNT_POINTS) {
        LOGE(TAG, "Mount table full (%d/%d)", s_mount_count, MAX_MOUNT_POINTS);
        return VFS_ERR_FULL;
    }
    
    /* 记录挂载点 */
    mount_point_t* mp = &s_mount_table[s_mount_count];
    strncpy(mp->virtual_path, virtual_path, MAX_PATH_LEN - 1);
    strncpy(mp->device_name, device_name, 15);
    mp->offset = offset;
    mp->size = size;
    mp->mounted = true;
    mp->read_only = false;
    mp->blockdev_handle = blockdev_handle;
    s_mount_count++;
    
    LOGI(TAG, "Registered mount point: %s (device=%s, offset=%zu, size=%zu)",
         virtual_path, device_name, offset, size);
    return VFS_OK;
}

vfs_err_t vfs_unmount(const char* virtual_path) {
    if (!s_initialized) {
        return VFS_ERR_NOT_INIT;
    }
    
    if (!virtual_path) {
        return VFS_ERR_PARAM;
    }
    
    int idx = find_mount_index(virtual_path);
    if (idx < 0) {
        LOGW(TAG, "Path '%s' not mounted", virtual_path);
        return VFS_ERR_NOT_MOUNTED;
    }
    
    LOGI(TAG, "Unmounting '%s'", virtual_path);
    
    /* 先从 ESP VFS 注销 LittleFS */
    esp_err_t err = esp_vfs_littlefs_unregister(virtual_path);
    if (err != ESP_OK) {
        LOGW(TAG, "LittleFS unregister failed for '%s': %s (continuing cleanup)",
             virtual_path, esp_err_to_name(err));
    }
    
    /* 从表中移除 */
    for (int i = idx; i < s_mount_count - 1; i++) {
        memcpy(&s_mount_table[i], &s_mount_table[i + 1], sizeof(mount_point_t));
    }
    s_mount_count--;
    
    LOGI(TAG, "Unmounted: %s", virtual_path);
    return VFS_OK;
}

bool vfs_is_mounted(const char* path) {
    return find_mount_for_path(path) >= 0;
}

int vfs_get_mount_count(void) {
    return s_mount_count;
}

vfs_err_t vfs_get_mount_info(int index, vfs_mount_info_t* info) {
    if (!s_initialized || !info) {
        return VFS_ERR_PARAM;
    }
    
    if (index < 0 || index >= s_mount_count) {
        return VFS_ERR_PARAM;
    }
    
    const mount_point_t* mp = &s_mount_table[index];
    strncpy(info->virtual_path, mp->virtual_path, 31);
    strncpy(info->device_name, mp->device_name, 15);
    info->offset = mp->offset;
    info->size = mp->size;
    info->mounted = mp->mounted;
    
    return VFS_OK;
}

void vfs_print_status(void) {
    LOGI(TAG, "=== VFS Status ===");
    LOGI(TAG, "Initialized: %s", s_initialized ? "YES" : "NO");
    LOGI(TAG, "Mount points: %d/%d", s_mount_count, MAX_MOUNT_POINTS);
    
    if (s_mount_count > 0) {
        LOGI(TAG, "");
        LOGI(TAG, "  %-12s  %-12s  %-10s  %-10s  %s",
             "Path", "Device", "Offset", "Size", "Status");
        LOGI(TAG, "  %-12s  %-12s  %-10s  %-10s  %s",
             "------------", "------------", "----------", "----------", "------");
        
        for (int i = 0; i < s_mount_count; i++) {
            const mount_point_t* mp = &s_mount_table[i];
            LOGI(TAG, "  %-12s  %-12s  %-10u  %-10u  %s",
                 mp->virtual_path, mp->device_name,
                 mp->offset, mp->size,
                 mp->mounted ? "MOUNTED" : "UNMOUNTED");
        }
    }
    
    /* 打印块设备 */
    LOGI(TAG, "");
    vfs_block_dev_print_all();
}

/* ========== 便捷 API 实现 ========== */

vfs_err_t vfs_unmount_all(void) {
    if (!s_initialized) {
        return VFS_ERR_NOT_INIT;
    }
    
    LOGI(TAG, "Unmounting all filesystems...");
    
    /* 从后向前卸载，避免索引变化 */
    for (int i = s_mount_count - 1; i >= 0; i--) {
        if (s_mount_table[i].mounted) {
            vfs_err_t err = vfs_unmount(s_mount_table[i].virtual_path);
            if (err != VFS_OK) {
                LOGW(TAG, "Failed to unmount '%s': %d", s_mount_table[i].virtual_path, err);
            }
        }
    }
    
    LOGI(TAG, "All filesystems unmounted");
    return VFS_OK;
}

vfs_err_t vfs_format(const char* virtual_path) {
    if (!s_initialized) {
        return VFS_ERR_NOT_INIT;
    }
    
    if (!virtual_path) {
        return VFS_ERR_PARAM;
    }
    
    int idx = find_mount_index(virtual_path);
    if (idx < 0) {
        LOGE(TAG, "Path '%s' not mounted", virtual_path);
        return VFS_ERR_NOT_MOUNTED;
    }
    
    const mount_point_t* mp = &s_mount_table[idx];
    
    LOGI(TAG, "Formatting LittleFS '%s'...", mp->virtual_path);
    
    /* 检查 blockdev 句柄是否有效 */
    if (!mp->blockdev_handle) {
        LOGE(TAG, "No blockdev handle for '%s', cannot format", virtual_path);
        return VFS_ERR_NO_DEV;
    }
    
    /* 使用 LittleFS blockdev 格式化 API */
    esp_err_t err = esp_littlefs_format_blockdev((esp_blockdev_handle_t)mp->blockdev_handle);
    if (err != ESP_OK) {
        LOGE(TAG, "LittleFS format failed for '%s': %s", virtual_path, esp_err_to_name(err));
        return VFS_ERR_IO;
    }
    
    LOGI(TAG, "Format complete: %s", virtual_path);
    return VFS_OK;
}

vfs_err_t vfs_get_mount_info_by_path(const char* path, vfs_mount_info_t* info) {
    if (!s_initialized || !path || !info) {
        return VFS_ERR_PARAM;
    }
    
    int idx = find_mount_for_path(path);
    if (idx < 0) {
        return VFS_ERR_NOT_MOUNTED;
    }
    
    const mount_point_t* mp = &s_mount_table[idx];
    strncpy(info->virtual_path, mp->virtual_path, 31);
    strncpy(info->device_name, mp->device_name, 15);
    info->offset = mp->offset;
    info->size = mp->size;
    info->mounted = mp->mounted;
    
    return VFS_OK;
}
