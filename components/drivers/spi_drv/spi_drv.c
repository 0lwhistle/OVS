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
#include "logger.h"

#include "driver/spi_master.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
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
    bool initialized;                 /**< 初始化标志 */
    int device_count;                 /**< 设备数量 */
    spi_dev_ctx_t devices[4];        /**< 设备上下文数组（最多4个） */
};

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
    void* copy = heap_caps_malloc(size, MALLOC_CAP_DMA);
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

spi_drv_err_t spi_drv_load_config(spi_drv_config_t* config) {
    if (!config) {
        return SPI_DRV_ERR_PARAM;
    }
    
    dtree_err_t err;
    
    err = DTREE_INT("spi.bus", "sclk_pin", &config->sclk_pin);
    if (err != DTREE_OK) {
        LOGE(TAG, "Read sclk_pin failed: %d", err);
        return SPI_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT("spi.bus", "miso_pin", &config->miso_pin);
    if (err != DTREE_OK) {
        LOGE(TAG, "Read miso_pin failed: %d", err);
        return SPI_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT("spi.bus", "mosi_pin", &config->mosi_pin);
    if (err != DTREE_OK) {
        LOGE(TAG, "Read mosi_pin failed: %d", err);
        return SPI_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT("spi.bus", "max_freq_mhz", &config->max_freq_mhz);
    if (err != DTREE_OK) {
        LOGE(TAG, "Read max_freq_mhz failed: %d", err);
        return SPI_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT("spi.bus", "mode", &config->mode);
    if (err != DTREE_OK) {
        LOGE(TAG, "Read mode failed: %d", err);
        return SPI_DRV_ERR_CONFIG;
    }
    
    LOGI(TAG, "SPI config: sclk=%" PRId32 ", miso=%" PRId32 ", mosi=%" PRId32 
         ", freq=%" PRId32 "MHz, mode=%" PRId32,
         config->sclk_pin, config->miso_pin, config->mosi_pin, 
         config->max_freq_mhz, config->mode);
    
    return SPI_DRV_OK;
}

spi_drv_err_t spi_drv_init(const spi_drv_config_t* config, spi_drv_handle_t* handle) {
    if (!config || !handle) {
        return SPI_DRV_ERR_PARAM;
    }
    
    LOGI(TAG, "Initializing SPI driver...");
    
    // 分配句柄
    struct spi_drv_handle* h = calloc(1, sizeof(struct spi_drv_handle));
    if (!h) {
        LOGE(TAG, "Failed to allocate handle");
        return SPI_DRV_ERR_HW;
    }
    
    // 配置SPI总线
    spi_bus_config_t bus_config = {
        .mosi_io_num = (int)config->mosi_pin,
        .miso_io_num = (int)config->miso_pin,
        .sclk_io_num = (int)config->sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,  // 默认最大传输
    };
    
    spi_host_device_t host = SPI2_HOST;
    
    // 初始化SPI总线（启用DMA）
    esp_err_t ret = spi_bus_initialize(host, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        free(h);
        return SPI_DRV_ERR_HW;
    }
    
    h->host = host;
    h->bus_config = bus_config;
    h->initialized = true;
    h->device_count = 0;
    
    *handle = h;
    
    LOGI(TAG, "SPI driver initialized (host=%d, DMA enabled)", host);
    return SPI_DRV_OK;
}

spi_drv_err_t spi_drv_deinit(spi_drv_handle_t handle) {
    if (!handle) {
        return SPI_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return SPI_DRV_ERR_NOT_INIT;
    }
    
    LOGI(TAG, "Deinitializing SPI driver...");
    
    // 释放所有挂起的异步传输
    for (int i = 0; i < 4; i++) {
        if (handle->devices[i].pending_xfer) {
            free(handle->devices[i].pending_xfer);
        }
    }
    
    esp_err_t ret = spi_bus_free(handle->host);
    if (ret != ESP_OK) {
        LOGE(TAG, "SPI bus free failed: %s", esp_err_to_name(ret));
    }
    
    handle->initialized = false;
    free(handle);
    
    LOGI(TAG, "SPI driver deinitialized");
    return SPI_DRV_OK;
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
        free(ctx->pending_xfer);
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
    void* rx_safe = rx_data ? heap_caps_malloc(size, MALLOC_CAP_DMA) : NULL;
    
    if ((tx_data && !tx_safe) || (rx_data && !rx_safe)) {
        free(tx_copy);
        free(rx_safe);
        return SPI_DRV_ERR_DMA;
    }
    
    // 配置传输
    spi_transaction_t trans = {
        .length = size * 8,
        .rxlength = rx_data ? size * 8 : 0,
        .tx_buffer = tx_safe,
        .rx_buffer = rx_safe,
    };
    
    // 执行传输
    esp_err_t ret = spi_device_polling_transmit(dev_handle, &trans);
    
    // 复制接收数据
    if (rx_data && rx_safe) {
        memcpy(rx_data, rx_safe, size);
    }
    
    // 清理
    free(tx_copy);
    free(rx_safe);
    
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
    spi_transaction_t* trans = heap_caps_malloc(sizeof(spi_transaction_t), MALLOC_CAP_DMA);
    if (!trans) {
        return SPI_DRV_ERR_DMA;
    }
    memset(trans, 0, sizeof(spi_transaction_t));
    
    // 分配DMA缓冲区并复制数据
    void* buf = heap_caps_malloc(size, MALLOC_CAP_DMA);
    if (!buf) {
        free(trans);
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
        free(buf);
        free(trans);
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
        free(buf);
        free(ctx->pending_xfer);
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
