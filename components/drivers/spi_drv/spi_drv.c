/**
 * @file spi_drv.c
 * @brief SPI驱动实现（支持DMA异步传输）
 * 
 * 设计要点：
 * 1. DMA传输减少CPU占用，适合大块数据（LCD帧、音频）
 * 2. 支持多设备共享总线，每个设备独立管理异步状态
 * 3. 小数据量自动降级为轮询模式
 * 4. 内存对齐处理，确保DMA兼容
 */

#include "spi_drv.h"
#include "mem.h"
#include "logger.h"

#include "driver/spi_master.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static const char* TAG = "[SPI_DRV]";

/* ========================================================================== */
/*                              内部数据结构                                   */
/* ========================================================================== */

/** DMA传输阈值：小于该值使用轮询，大于使用DMA */
#define DMA_THRESHOLD_BYTES     64

/** DMA安全缓冲区对齐 */
#define DMA_BUF_ALIGN           4

/**
 * @brief SPI设备上下文
 */
typedef struct spi_dev_ctx {
    spi_device_handle_t handle;      /**< ESP-IDF设备句柄 */
    spi_xfer_mode_t xfer_mode;      /**< 默认传输模式 */
    int max_transfer_sz;             /**< 最大传输大小 */
    spi_transaction_t* pending_xfer; /**< 挂起的异步传输 */
    bool async_busy;                 /**< 异步传输进行中 */
} spi_dev_ctx_t;

/**
 * @brief SPI驱动句柄结构
 */
struct spi_drv_handle {
    spi_host_device_t host;          /**< SPI主机设备 */
    spi_bus_config_t bus_config;      /**< 总线配置 */
    spi_drv_config_t drv_config;      /**< 用户配置（共享一致性校验） */
    bool initialized;                 /**< 初始化标志 */
    int device_count;                 /**< 设备数量 */
    spi_dev_ctx_t devices[4];        /**< 设备上下文数组（最多4个） */
    uint32_t ref_count;               /**< 引用计数（共享总线模型） */
};

/* ========================================================================== */
/*                    共享总线注册表（引用计数模型）                            */
/* ========================================================================== */

/**
 * Linux 式共享计数设计：
 * - 同一 SPI 总线底层硬件只初始化一次；
 * - 多个设备模块（ST7789、W25Q128 等）调用 spi_drv_init() 获取同一个
 *   总线句柄，引用计数 +1；
 * - spi_drv_deinit() 只释放自己的引用，引用计数归零时才真正销毁总线。
 *
 * 设备树通过 bus.host 显式指定控制器（spi2 / spi3），
 * 本文件维护两张表项：host=2 → SPI2_HOST，host=3 → SPI3_HOST。
 */
#define SPI_DRV_MAX_HOSTS 2
static spi_drv_handle_t s_spi_buses[SPI_DRV_MAX_HOSTS] = { NULL };
static SemaphoreHandle_t s_spi_bus_lock = NULL;

static int spi_host_to_slot(int32_t host_id) {
    if (host_id == 2) return 0;
    if (host_id == 3) return 1;
    return -1;
}

static spi_host_device_t spi_host_to_hw(int32_t host_id, bool* ok) {
    if (host_id == 2) { *ok = true; return SPI2_HOST; }
    if (host_id == 3) { *ok = true; return SPI3_HOST; }
    *ok = false;
    return SPI2_HOST;
}

static bool spi_bus_lock_take(void) {
    if (!s_spi_bus_lock) {
        s_spi_bus_lock = xSemaphoreCreateMutex();
    }
    if (!s_spi_bus_lock) {
        LOGE(TAG, "Failed to create SPI bus lock");
        return false;
    }
    xSemaphoreTake(s_spi_bus_lock, portMAX_DELAY);
    return true;
}

static void spi_bus_lock_give(void) {
    if (s_spi_bus_lock) {
        xSemaphoreGive(s_spi_bus_lock);
    }
}

static bool spi_drv_config_equal(const spi_drv_config_t* a, const spi_drv_config_t* b) {
    return a && b
        && a->host == b->host
        && a->sclk_pin == b->sclk_pin
        && a->miso_pin == b->miso_pin
        && a->mosi_pin == b->mosi_pin
        && a->max_freq_mhz == b->max_freq_mhz
        && a->mode == b->mode;
}

/**
 * @brief 真正销毁 SPI 总线（引用计数归零时调用）
 */
static spi_drv_err_t spi_bus_destroy(int slot) {
    if (slot < 0 || slot >= SPI_DRV_MAX_HOSTS) {
        return SPI_DRV_ERR_PARAM;
    }

    spi_drv_handle_t h = s_spi_buses[slot];
    if (!h) {
        return SPI_DRV_OK;
    }

    /* 删除仍然挂在总线上的设备，避免 spi_bus_free 返回 INVALID_STATE */
    for (int i = 0; i < 4; i++) {
        if (h->devices[i].handle) {
            /* 先等待尚未完成的异步传输，再释放其结构，防止队列悬垂引用 */
            if (h->devices[i].async_busy) {
                spi_transaction_t* result = NULL;
                spi_device_get_trans_result(h->devices[i].handle, &result, portMAX_DELAY);
            }
            if (h->devices[i].pending_xfer) {
                void* tx_buf = (void*)h->devices[i].pending_xfer->tx_buffer;
                if (tx_buf) {
                    mem_free(tx_buf);
                }
                mem_free(h->devices[i].pending_xfer);
                h->devices[i].pending_xfer = NULL;
            }
            h->devices[i].async_busy = false;

            esp_err_t err = spi_bus_remove_device(h->devices[i].handle);
            if (err != ESP_OK) {
                LOGW(TAG, "Remove device slot %d failed: %s", i, esp_err_to_name(err));
            }
            h->devices[i].handle = NULL;
        }
    }

    esp_err_t err = spi_bus_free(h->host);
    if (err != ESP_OK) {
        LOGE(TAG, "SPI bus free failed: %s", esp_err_to_name(err));
        return SPI_DRV_ERR_HW;
    }

    mem_free(h);
    s_spi_buses[slot] = NULL;
    return SPI_DRV_OK;
}

/* ========================================================================== */
/*                              内部辅助函数                                   */
/* ========================================================================== */

/**
 * @brief 查找设备上下文
 */
static spi_dev_ctx_t* find_dev_ctx(spi_drv_handle_t handle, spi_device_handle_t dev) {
    for (int i = 0; i < 4; i++) {
        if (handle->devices[i].handle == dev) {
            return &handle->devices[i];
        }
    }
    return NULL;
}

/**
 * @brief 分配DMA安全缓冲区
 * 
 * 如果数据不在DMA安全内存中，会复制一份
 */
static const void* ensure_dma_safe(const void* data, size_t size, void** out_copy) {
    *out_copy = NULL;
    
    // 检查是否DMA安全（在SRAM中）
    if (esp_ptr_dma_capable(data)) {
        return data;
    }
    
    // 需要复制到DMA安全内存
    void* copy = mem_dma_alloc(size);
    if (!copy) {
        LOGE(TAG, "Failed to allocate DMA buffer (%d bytes)", size);
        return NULL;
    }
    memcpy(copy, data, size);
    *out_copy = copy;
    return copy;
}

/* ========================================================================== */
/*                              公共API实现                                    */
/* ========================================================================== */

/* ---------- 初始化/反初始化 ---------- */

spi_drv_err_t spi_drv_load_config(dtree_node_t* bus_node, spi_drv_config_t* config) {
    if (!bus_node || !config) {
        return SPI_DRV_ERR_PARAM;
    }

    /* 总线节点 compatible 校验，防止把错误的节点当总线 */
    const char* compat = dtree_get_compatible(bus_node);
    if (!compat || strcmp(compat, "esp32s3-spi") != 0) {
        LOGE(TAG, "Bus node compatible '%s' is not 'esp32s3-spi'",
             compat ? compat : "null");
        return SPI_DRV_ERR_CONFIG;
    }

    dtree_err_t err;

    /* 控制器编号来自节点名，例如 "spi2"、"spi3" */
    err = dtree_get_host_id(bus_node, "spi", &config->host);
    if (err != DTREE_OK) {
        LOGE(TAG, "Read spi host id failed: %d", err);
        return SPI_DRV_ERR_CONFIG;
    }
    if (spi_host_to_slot(config->host) < 0) {
        LOGE(TAG, "Unsupported SPI host id: %" PRId32, config->host);
        return SPI_DRV_ERR_CONFIG;
    }

    err = dtree_get_int(bus_node, "sclk_pin", &config->sclk_pin);
    if (err != DTREE_OK) {
        LOGE(TAG, "Read sclk_pin failed: %d", err);
        return SPI_DRV_ERR_CONFIG;
    }

    err = dtree_get_int(bus_node, "miso_pin", &config->miso_pin);
    if (err != DTREE_OK) {
        LOGE(TAG, "Read miso_pin failed: %d", err);
        return SPI_DRV_ERR_CONFIG;
    }

    err = dtree_get_int(bus_node, "mosi_pin", &config->mosi_pin);
    if (err != DTREE_OK) {
        LOGE(TAG, "Read mosi_pin failed: %d", err);
        return SPI_DRV_ERR_CONFIG;
    }

    err = dtree_get_int(bus_node, "max_freq_mhz", &config->max_freq_mhz);
    if (err != DTREE_OK) {
        LOGE(TAG, "Read max_freq_mhz failed: %d", err);
        return SPI_DRV_ERR_CONFIG;
    }

    err = dtree_get_int(bus_node, "mode", &config->mode);
    if (err != DTREE_OK) {
        LOGE(TAG, "Read mode failed: %d", err);
        return SPI_DRV_ERR_CONFIG;
    }
    
    LOGI(TAG, "SPI config: host=spi%" PRId32 ", sclk=%" PRId32 ", miso=%" PRId32
         ", mosi=%" PRId32 ", freq=%" PRId32 "MHz, mode=%" PRId32,
         config->host, config->sclk_pin, config->miso_pin, config->mosi_pin,
         config->max_freq_mhz, config->mode);
    
    return SPI_DRV_OK;
}

spi_drv_err_t spi_drv_init(const spi_drv_config_t* config, spi_drv_handle_t* handle) {
    if (!config || !handle) {
        return SPI_DRV_ERR_PARAM;
    }

    if (!spi_bus_lock_take()) {
        return SPI_DRV_ERR_HW;
    }

    int slot = spi_host_to_slot(config->host);
    if (slot < 0) {
        LOGE(TAG, "Unsupported SPI host id: %" PRId32, config->host);
        spi_bus_lock_give();
        return SPI_DRV_ERR_CONFIG;
    }

    /* 对应 host 的总线已存在：校验配置一致性后共享句柄（引用计数 +1） */
    spi_drv_handle_t existing = s_spi_buses[slot];
    if (existing) {
        if (!spi_drv_config_equal(config, &existing->drv_config)) {
            LOGE(TAG, "SPI host spi%" PRId32 " already initialized with different config",
                 config->host);
            spi_bus_lock_give();
            return SPI_DRV_ERR_CONFIG;
        }

        existing->ref_count++;
        *handle = existing;
        LOGI(TAG, "SPI host spi%" PRId32 " shared: ref=%" PRIu32,
             config->host, existing->ref_count);
        spi_bus_lock_give();
        return SPI_DRV_OK;
    }

    /* 首次获取：真正初始化硬件 */
    struct spi_drv_handle* h = mem_calloc(1, sizeof(struct spi_drv_handle));
    if (!h) {
        LOGE(TAG, "Failed to allocate handle");
        spi_bus_lock_give();
        return SPI_DRV_ERR_HW;
    }

    spi_bus_config_t bus_config = {
        .mosi_io_num = (int)config->mosi_pin,
        .miso_io_num = (int)config->miso_pin,
        .sclk_io_num = (int)config->sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    bool host_ok = false;
    spi_host_device_t host = spi_host_to_hw(config->host, &host_ok);
    if (!host_ok) {
        LOGE(TAG, "Unsupported SPI host id: %" PRId32, config->host);
        mem_free(h);
        spi_bus_lock_give();
        return SPI_DRV_ERR_CONFIG;
    }

    esp_err_t ret = spi_bus_initialize(host, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        mem_free(h);
        spi_bus_lock_give();
        return SPI_DRV_ERR_HW;
    }

    h->host = host;
    h->bus_config = bus_config;
    h->drv_config = *config;
    h->initialized = true;
    h->device_count = 0;
    h->ref_count = 1;

    s_spi_buses[slot] = h;
    *handle = h;

    LOGI(TAG, "SPI host spi%" PRId32 " initialized (host=%d, DMA enabled, ref=1)",
         config->host, (int)host);
    spi_bus_lock_give();
    return SPI_DRV_OK;
}

spi_drv_err_t spi_drv_deinit(spi_drv_handle_t handle) {
    if (!handle) {
        return SPI_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return SPI_DRV_ERR_NOT_INIT;
    }

    if (!spi_bus_lock_take()) {
        return SPI_DRV_ERR_HW;
    }

    int slot = -1;
    for (int i = 0; i < SPI_DRV_MAX_HOSTS; i++) {
        if (s_spi_buses[i] == handle) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        LOGE(TAG, "Handle is not owned by SPI driver registry");
        spi_bus_lock_give();
        return SPI_DRV_ERR_PARAM;
    }

    if (handle->ref_count == 0) {
        LOGW(TAG, "SPI bus already fully released");
        spi_bus_lock_give();
        return SPI_DRV_OK;
    }

    /* 还有其他使用者：只释放自己的引用 */
    if (handle->ref_count > 1) {
        handle->ref_count--;
        LOGI(TAG, "SPI driver released one reference (remaining=%" PRIu32 ")",
             handle->ref_count);
        spi_bus_lock_give();
        return SPI_DRV_OK;
    }

    LOGI(TAG, "Deinitializing SPI driver (last reference)...");
    spi_drv_err_t err = spi_bus_destroy(slot);
    spi_bus_lock_give();
    return err;
}

/* ---------- 设备管理 ---------- */

spi_drv_err_t spi_drv_add_device(spi_drv_handle_t handle, 
                                  const spi_dev_config_t* config,
                                  spi_device_handle_t* dev_handle) {
    if (!handle || !config || !dev_handle) {
        return SPI_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return SPI_DRV_ERR_NOT_INIT;
    }
    
    // 查找空闲设备槽位
    int slot = -1;
    for (int i = 0; i < 4; i++) {
        if (!handle->devices[i].handle) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        LOGE(TAG, "No free device slot");
        return SPI_DRV_ERR_HW;
    }
    
    LOGI(TAG, "Adding SPI device: cs=%d, speed=%d Hz, mode=%d", 
         config->cs_pin, config->clock_speed_hz, config->mode);
    
    // 配置设备接口
    spi_device_interface_config_t dev_config = {
        .clock_speed_hz = config->clock_speed_hz,
        .mode = (uint8_t)config->mode,
        .spics_io_num = config->cs_pin,
        .queue_size = 4,  // 支持多个异步传输排队
        .flags = 0,
        .duty_cycle_pos = 128,
    };
    
    spi_device_handle_t device;
    esp_err_t ret = spi_bus_add_device(handle->host, &dev_config, &device);
    if (ret != ESP_OK) {
        LOGE(TAG, "Add device failed: %s", esp_err_to_name(ret));
        return SPI_DRV_ERR_HW;
    }
    
    // 保存设备上下文
    handle->devices[slot].handle = device;
    handle->devices[slot].xfer_mode = config->xfer_mode;
    handle->devices[slot].max_transfer_sz = config->max_transfer_sz;
    handle->devices[slot].async_busy = false;
    handle->devices[slot].pending_xfer = NULL;
    
    handle->device_count++;
    *dev_handle = device;
    
    LOGI(TAG, "SPI device added (slot=%d, devices=%d)", slot, handle->device_count);
    return SPI_DRV_OK;
}

spi_drv_err_t spi_drv_remove_device(spi_drv_handle_t handle, spi_device_handle_t dev_handle) {
    if (!handle || !dev_handle) {
        return SPI_DRV_ERR_PARAM;
    }
    
    spi_dev_ctx_t* ctx = find_dev_ctx(handle, dev_handle);
    if (!ctx) {
        LOGE(TAG, "Device not found");
        return SPI_DRV_ERR_PARAM;
    }
    
    // 等待异步传输完成
    if (ctx->async_busy) {
        LOGW(TAG, "Waiting for async transfer before remove...");
        spi_drv_wait_async(handle, dev_handle, 1000);
    }
    
    // 释放挂起的传输
    if (ctx->pending_xfer) {
        mem_free(ctx->pending_xfer);
        ctx->pending_xfer = NULL;
    }
    
    esp_err_t ret = spi_bus_remove_device(dev_handle);
    if (ret != ESP_OK) {
        LOGE(TAG, "Remove device failed: %s", esp_err_to_name(ret));
        return SPI_DRV_ERR_HW;
    }
    
    ctx->handle = NULL;
    handle->device_count--;
    
    LOGI(TAG, "SPI device removed (devices=%d)", handle->device_count);
    return SPI_DRV_OK;
}

/* ---------- 同步传输（阻塞） ---------- */

spi_drv_err_t spi_drv_transfer(spi_drv_handle_t handle, 
                                spi_device_handle_t dev_handle,
                                const void* tx_data, 
                                void* rx_data, 
                                size_t size) {
    if (!handle || (!tx_data && !rx_data)) {
        return SPI_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return SPI_DRV_ERR_NOT_INIT;
    }
    
    if (size == 0) {
        return SPI_DRV_OK;
    }
    
    // 确保数据在DMA安全内存中
    void* tx_copy = NULL;
    void* rx_copy __attribute__((unused)) = NULL;
    const void* tx_safe = tx_data ? ensure_dma_safe(tx_data, size, &tx_copy) : NULL;
    void* rx_safe = rx_data ? mem_dma_alloc(size) : NULL;
    
    if ((tx_data && !tx_safe) || (rx_data && !rx_safe)) {
        mem_free(tx_copy);
        mem_free(rx_safe);
        return SPI_DRV_ERR_DMA;
    }
    
    // 配置传输
    spi_transaction_t trans = {
        .length = size * 8,
        .rxlength = rx_data ? size * 8 : 0,
        .tx_buffer = tx_safe,
        .rx_buffer = rx_safe,
    };
    
    // 执行传输（队列+等待：传输期间调用任务休眠而非自旋占用 CPU；
    // 总线由 IDF 按事务粒度仲裁，共享总线上的其他设备事务可在事务间插空）
    esp_err_t ret = spi_device_transmit(dev_handle, &trans);
    
    // 复制接收数据
    if (rx_data && rx_safe) {
        memcpy(rx_data, rx_safe, size);
    }
    
    // 清理
    mem_free(tx_copy);
    mem_free(rx_safe);
    
    if (ret != ESP_OK) {
        LOGE(TAG, "Transfer failed: %s", esp_err_to_name(ret));
        return SPI_DRV_ERR_HW;
    }
    
    return SPI_DRV_OK;
}

spi_drv_err_t spi_drv_write(spi_drv_handle_t handle, 
                             spi_device_handle_t dev_handle,
                             const void* data, 
                             size_t size) {
    return spi_drv_transfer(handle, dev_handle, data, NULL, size);
}

spi_drv_err_t spi_drv_read(spi_drv_handle_t handle, 
                            spi_device_handle_t dev_handle,
                            void* buffer, 
                            size_t size) {
    return spi_drv_transfer(handle, dev_handle, NULL, buffer, size);
}

/* ---------- DMA 异步传输（非阻塞） ---------- */

spi_drv_err_t spi_drv_write_async(spi_drv_handle_t handle, 
                                    spi_device_handle_t dev_handle,
                                    const void* data, 
                                    size_t size,
                                    spi_dma_callback_t callback,
                                    void* cb_arg) {
    if (!handle || !data || size == 0) {
        return SPI_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return SPI_DRV_ERR_NOT_INIT;
    }
    
    spi_dev_ctx_t* ctx = find_dev_ctx(handle, dev_handle);
    if (!ctx) {
        return SPI_DRV_ERR_PARAM;
    }
    
    // 等待之前的异步传输完成
    if (ctx->async_busy) {
        spi_drv_wait_async(handle, dev_handle, -1);
    }
    
    // 分配传输结构
    spi_transaction_t* trans = mem_dma_alloc(sizeof(spi_transaction_t));
    if (!trans) {
        return SPI_DRV_ERR_DMA;
    }
    memset(trans, 0, sizeof(spi_transaction_t));
    
    // 分配DMA缓冲区并复制数据
    void* buf = mem_dma_alloc(size);
    if (!buf) {
        mem_free(trans);
        return SPI_DRV_ERR_DMA;
    }
    memcpy(buf, data, size);
    
    // 配置传输
    trans->length = size * 8;
    trans->tx_buffer = buf;
    trans->rx_buffer = NULL;
    trans->user = cb_arg;  // 回调参数
    
    // 启动异步传输
    ctx->async_busy = true;
    ctx->pending_xfer = trans;
    
    esp_err_t ret = spi_device_queue_trans(dev_handle, trans, 0);
    if (ret != ESP_OK) {
        ctx->async_busy = false;
        ctx->pending_xfer = NULL;
        mem_free(buf);
        mem_free(trans);
        LOGE(TAG, "Queue async failed: %s", esp_err_to_name(ret));
        return SPI_DRV_ERR_HW;
    }
    
    // 异步模式下，这里不等待完成，由用户调用 wait_async 或回调通知
    
    return SPI_DRV_OK;
}

spi_drv_err_t spi_drv_wait_async(spi_drv_handle_t handle, 
                                   spi_device_handle_t dev_handle,
                                   int timeout_ms) {
    if (!handle) {
        return SPI_DRV_ERR_PARAM;
    }
    
    spi_dev_ctx_t* ctx = find_dev_ctx(handle, dev_handle);
    if (!ctx || !ctx->async_busy) {
        return SPI_DRV_OK;  // 没有挂起的传输
    }
    
    // 等待传输完成
    spi_transaction_t* result = NULL;
    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    
    esp_err_t ret = spi_device_get_trans_result(dev_handle, &result, ticks);
    
    // 清理
    if (ctx->pending_xfer) {
        // 释放DMA缓冲区（在trans->tx_buffer中）
        void* buf = (void*)ctx->pending_xfer->tx_buffer;
        mem_free(buf);
        mem_free(ctx->pending_xfer);
        ctx->pending_xfer = NULL;
    }
    
    ctx->async_busy = false;
    
    if (ret != ESP_OK) {
        LOGE(TAG, "Wait async failed: %s", esp_err_to_name(ret));
        return SPI_DRV_ERR_TIMEOUT;
    }
    
    return SPI_DRV_OK;
}

/* ---------- 特殊传输（LCD等） ---------- */

spi_drv_err_t spi_drv_write_cmd_data(spi_drv_handle_t handle,
                                       spi_device_handle_t dev_handle,
                                       int dc_pin,
                                       uint8_t cmd,
                                       const void* data,
                                       size_t data_len) {
    if (!handle) {
        return SPI_DRV_ERR_PARAM;
    }
    
    // 发送命令（DC低）
    if (dc_pin >= 0) {
        gpio_set_level(dc_pin, 0);
    }
    
    spi_drv_err_t err = spi_drv_write(handle, dev_handle, &cmd, 1);
    if (err != SPI_DRV_OK) {
        return err;
    }
    
    // 发送数据（DC高）
    if (data && data_len > 0) {
        if (dc_pin >= 0) {
            gpio_set_level(dc_pin, 1);
        }
        err = spi_drv_write(handle, dev_handle, data, data_len);
    }
    
    return err;
}

spi_drv_err_t spi_drv_write_cmd_data_async(spi_drv_handle_t handle,
                                             spi_device_handle_t dev_handle,
                                             int dc_pin,
                                             uint8_t cmd,
                                             const void* data,
                                             size_t data_len,
                                             spi_dma_callback_t callback,
                                             void* cb_arg) {
    // 命令部分同步发送（通常只有1字节，没必要异步）
    if (dc_pin >= 0) {
        gpio_set_level(dc_pin, 0);
    }
    
    spi_drv_err_t err = spi_drv_write(handle, dev_handle, &cmd, 1);
    if (err != SPI_DRV_OK) {
        return err;
    }
    
    // 数据部分异步发送
    if (data && data_len > 0) {
        if (dc_pin >= 0) {
            gpio_set_level(dc_pin, 1);
        }
        
        // 小数据量同步发送
        if (data_len < DMA_THRESHOLD_BYTES) {
            err = spi_drv_write(handle, dev_handle, data, data_len);
        } else {
            err = spi_drv_write_async(handle, dev_handle, data, data_len, callback, cb_arg);
        }
    }
    
    return err;
}
