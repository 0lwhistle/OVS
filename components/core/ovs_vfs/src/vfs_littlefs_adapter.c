/**
 * @file vfs_littlefs_adapter.c
 * @brief VFS LittleFS 适配层
 * 
 * 将 VFS 块设备接口适配为 esp_blockdev 接口，
 * 以便使用 esp_vfs_littlefs_register() 挂载 LittleFS。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "ovs_vfs.h"
#include "ovs_vfs_block_dev.h"
#include "logger.h"
#include "esp_blockdev.h"
#include "esp_littlefs.h"

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static const char* TAG = "[VFS_LFS]";

/* ========== 适配层上下文 ========== */
typedef struct {
    const vfs_block_dev_t* vfs_dev;     /* VFS 块设备 */
    size_t partition_offset;            /* 分区偏移 */
    size_t partition_size;              /* 分区大小 */
    bool released;                      /* 是否已释放 */
} adapter_ctx_t;

/* ========== 全局静态块设备句柄池 ========== */
#define MAX_BLOCKDEV_HANDLES 8
static esp_blockdev_t s_blockdev_pool[MAX_BLOCKDEV_HANDLES];
static bool s_blockdev_used[MAX_BLOCKDEV_HANDLES] = {false};
static adapter_ctx_t s_ctx_pool[MAX_BLOCKDEV_HANDLES];

/* ========== esp_blockdev 回调实现 ========== */

static esp_err_t adapter_read(esp_blockdev_handle_t dev_handle, uint8_t* dst_buf, 
                               size_t dst_buf_size, uint64_t src_addr, size_t data_read_len) {
    adapter_ctx_t* ctx = (adapter_ctx_t*)dev_handle->ctx;
    
    /* 检查是否已释放 */
    if (ctx->released) {
        LOGE(TAG, "Block device already released");
        return ESP_ERR_INVALID_STATE;
    }
    
    /* 计算实际偏移 */
    size_t actual_offset = ctx->partition_offset + (size_t)src_addr;
    
    /* 检查边界 */
    if ((size_t)src_addr + data_read_len > ctx->partition_size) {
        LOGE(TAG, "Read out of bounds: addr=%llu, len=%zu, partition_size=%zu", 
             src_addr, data_read_len, ctx->partition_size);
        return ESP_ERR_INVALID_SIZE;
    }
    
    vfs_bd_err_t err = ctx->vfs_dev->ops->read(ctx->vfs_dev->priv, dst_buf, data_read_len, actual_offset);
    return (err == VFS_BD_OK) ? ESP_OK : ESP_FAIL;
}

static esp_err_t adapter_write(esp_blockdev_handle_t dev_handle, const uint8_t* src_buf,
                                uint64_t dst_addr, size_t data_write_len) {
    adapter_ctx_t* ctx = (adapter_ctx_t*)dev_handle->ctx;
    
    /* 检查是否已释放 */
    if (ctx->released) {
        LOGE(TAG, "Block device already released");
        return ESP_ERR_INVALID_STATE;
    }
    
    /* 计算实际偏移 */
    size_t actual_offset = ctx->partition_offset + (size_t)dst_addr;
    
    /* 检查边界 */
    if ((size_t)dst_addr + data_write_len > ctx->partition_size) {
        LOGE(TAG, "Write out of bounds: addr=%llu, len=%zu, partition_size=%zu", 
             dst_addr, data_write_len, ctx->partition_size);
        return ESP_ERR_INVALID_SIZE;
    }
    
    vfs_bd_err_t err = ctx->vfs_dev->ops->write(ctx->vfs_dev->priv, src_buf, data_write_len, actual_offset);
    return (err == VFS_BD_OK) ? ESP_OK : ESP_FAIL;
}

static esp_err_t adapter_erase(esp_blockdev_handle_t dev_handle, uint64_t start_addr, size_t erase_len) {
    adapter_ctx_t* ctx = (adapter_ctx_t*)dev_handle->ctx;
    
    /* 检查是否已释放 */
    if (ctx->released) {
        LOGE(TAG, "Block device already released");
        return ESP_ERR_INVALID_STATE;
    }
    
    /* 计算实际偏移 */
    size_t actual_offset = ctx->partition_offset + (size_t)start_addr;
    
    /* 检查边界 */
    if ((size_t)start_addr + erase_len > ctx->partition_size) {
        LOGE(TAG, "Erase out of bounds: addr=%llu, len=%zu, partition_size=%zu", 
             start_addr, erase_len, ctx->partition_size);
        return ESP_ERR_INVALID_SIZE;
    }
    
    vfs_bd_err_t err = ctx->vfs_dev->ops->erase(ctx->vfs_dev->priv, actual_offset, erase_len);
    return (err == VFS_BD_OK) ? ESP_OK : ESP_FAIL;
}

static esp_err_t adapter_sync(esp_blockdev_handle_t dev_handle) {
    /* 无需同步，直接返回成功 */
    return ESP_OK;
}

static esp_err_t adapter_ioctl(esp_blockdev_handle_t dev_handle, const uint8_t cmd, void* args) {
    /* 暂不支持 ioctl 命令 */
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t adapter_release(esp_blockdev_handle_t dev_handle) {
    if (dev_handle && dev_handle->ctx) {
        adapter_ctx_t* ctx = (adapter_ctx_t*)dev_handle->ctx;
        ctx->released = true;
        LOGI(TAG, "Block device adapter released");
    }
    return ESP_OK;
}

/* ========== 回调函数表 ========== */
static const esp_blockdev_ops_t s_adapter_ops = {
    .read = adapter_read,
    .write = adapter_write,
    .erase = adapter_erase,
    .sync = adapter_sync,
    .ioctl = adapter_ioctl,
    .release = adapter_release,
};

/* ========== 公共 API ========== */

esp_blockdev_handle_t vfs_create_blockdev_adapter(const vfs_block_dev_t* vfs_dev, 
                                                   size_t offset, size_t size) {
    if (!vfs_dev || !vfs_dev->ops) {
        LOGE(TAG, "Invalid VFS block device");
        return NULL;
    }
    
    /* 查找空闲的句柄槽位 */
    int slot = -1;
    for (int i = 0; i < MAX_BLOCKDEV_HANDLES; i++) {
        if (!s_blockdev_used[i]) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        LOGE(TAG, "No free block device handle slot");
        return NULL;
    }
    
    /* 初始化上下文 */
    adapter_ctx_t* ctx = &s_ctx_pool[slot];
    ctx->vfs_dev = vfs_dev;
    ctx->partition_offset = offset;
    ctx->partition_size = size;
    ctx->released = false;
    
    /* 初始化块设备句柄 */
    esp_blockdev_t* handle = &s_blockdev_pool[slot];
    memset(handle, 0, sizeof(esp_blockdev_t));
    
    /* 配置几何参数 */
    handle->ctx = ctx;
    handle->ops = &s_adapter_ops;
    handle->geometry.disk_size = (uint64_t)size;
    
    /* NOR Flash 参数:
     * - read_size: 1 (可以读取任意字节)
     * - write_size: 256 (页大小)
     * - erase_size: 4096 (扇区大小)
     * 
     * 注意: recommended_read_size 必须等于 read_size，
     * 否则会导致 cache_size 计算错误。
     */
    handle->geometry.read_size = 1;
    handle->geometry.write_size = 256;
    handle->geometry.erase_size = 4096;
    handle->geometry.recommended_read_size = 1;  /* 与 read_size 相同 */
    handle->geometry.recommended_write_size = 256;
    handle->geometry.recommended_erase_size = 4096;
    handle->device_flags.val = 0;
    handle->device_flags.erase_before_write = 1;
    handle->device_flags.and_type_write = 1;
    handle->device_flags.default_val_after_erase = 1;
    
    /* 标记槽位已使用 */
    s_blockdev_used[slot] = true;
    
    LOGI(TAG, "Created block device adapter: slot=%d, offset=%zu, size=%zu", 
         slot, offset, size);
    
    return handle;
}

void vfs_release_blockdev_adapter(esp_blockdev_handle_t handle) {
    if (!handle) return;
    
    /* 查找对应的槽位 */
    for (int i = 0; i < MAX_BLOCKDEV_HANDLES; i++) {
        if (&s_blockdev_pool[i] == handle) {
            s_blockdev_used[i] = false;
            LOGI(TAG, "Released block device adapter slot %d", i);
            return;
        }
    }
}

vfs_err_t vfs_mount_littlefs(const char* virtual_path, const char* device_name,
                              size_t offset, size_t size, bool format_if_fail) {
    if (!virtual_path || !device_name) {
        return VFS_ERR_PARAM;
    }
    
    /* 查找块设备 */
    const vfs_block_dev_t* dev = vfs_block_dev_get(device_name);
    if (!dev) {
        LOGE(TAG, "Block device '%s' not found", device_name);
        return VFS_ERR_NO_DEV;
    }
    
    /* 计算实际大小 */
    size_t actual_size = size;
    if (actual_size == 0) {
        actual_size = dev->ops->get_size(dev->priv) - offset;
    }
    
    /* 创建适配器 */
    esp_blockdev_handle_t bdl = vfs_create_blockdev_adapter(dev, offset, actual_size);
    if (!bdl) {
        LOGE(TAG, "Failed to create block device adapter");
        return VFS_ERR_NO_MEM;
    }
    
    /* 调试: 打印 blockdev 几何参数 */
    LOGI(TAG, "Blockdev geometry: disk_size=%" PRIu64 ", read=%zu, write=%zu, erase=%zu",
         bdl->geometry.disk_size,
         bdl->geometry.read_size,
         bdl->geometry.write_size,
         bdl->geometry.erase_size);
    
    /* 配置 LittleFS */
    esp_vfs_littlefs_conf_t conf = {
        .base_path = virtual_path,
        .blockdev = bdl,
        .format_if_mount_failed = format_if_fail,
        .dont_mount = false,
    };
    
    /* 挂载 LittleFS */
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        LOGE(TAG, "LittleFS mount failed for '%s': %s", virtual_path, esp_err_to_name(err));
        vfs_release_blockdev_adapter(bdl);
        return VFS_ERR_MOUNT;
    }
    
    /* 注册到 VFS 挂载表 */
    vfs_err_t reg_err = vfs_register_mount_point(virtual_path, device_name, offset, actual_size, (void*)bdl);
    if (reg_err != VFS_OK) {
        LOGW(TAG, "Failed to register mount point (filesystem still mounted): %d", reg_err);
    }
    
    LOGI(TAG, "LittleFS mounted: %s (device=%s, offset=%zu, size=%zu)", 
         virtual_path, device_name, offset, actual_size);
    
    return VFS_OK;
}
