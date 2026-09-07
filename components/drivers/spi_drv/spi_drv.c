/**
 * @file spi_drv.c
 * @brief SPI驱动实现
 * 
 * 实现SPI总线的初始化、配置和数据传输功能。
 * 硬件参数从设备树读取，不在代码中硬编码。
 * 使用ESP-IDF SPI Master API。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "spi_drv.h"
#include "logger.h"

#include "driver/spi_master.h"
#include "esp_err.h"
#include "driver/gpio.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static const char* TAG = "[SPI_DRV]";

/* ========================================================================== */
/*                              内部数据结构                                   */
/* ========================================================================== */

/**
 * @brief SPI驱动句柄结构
 * 
 * 保存SPI总线句柄和设备句柄，支持多个设备共享同一总线。
 */
struct spi_drv_handle {
    spi_host_device_t host;          /**< SPI主机设备 */
    spi_bus_config_t bus_config;      /**< 总线配置 */
    bool initialized;                 /**< 初始化标志 */
    int device_count;                 /**< 已注册的设备数量 */
};

/* ========================================================================== */
/*                              公共API实现                                    */
/* ========================================================================== */

spi_drv_err_t spi_drv_load_config(spi_drv_config_t* config) {
    if (!config) {
        return SPI_DRV_ERR_PARAM;
    }
    
    dtree_err_t err;
    
    err = DTREE_INT("spi.bus", "sclk_pin", &config->sclk_pin);
    if (DTREE_CHECK_ERROR("Read sclk_pin", err)) {
        return SPI_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT("spi.bus", "miso_pin", &config->miso_pin);
    if (DTREE_CHECK_ERROR("Read miso_pin", err)) {
        return SPI_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT("spi.bus", "mosi_pin", &config->mosi_pin);
    if (DTREE_CHECK_ERROR("Read mosi_pin", err)) {
        return SPI_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT("spi.bus", "max_freq_mhz", &config->max_freq_mhz);
    if (DTREE_CHECK_ERROR("Read max_freq_mhz", err)) {
        return SPI_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT("spi.bus", "mode", &config->mode);
    if (DTREE_CHECK_ERROR("Read mode", err)) {
        return SPI_DRV_ERR_CONFIG;
    }
    
    LOGI(TAG, "SPI config loaded: sclk=%" PRId32 ", miso=%" PRId32 ", mosi=%" PRId32 
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
    LOGI(TAG, "  SCLK pin: %" PRId32, config->sclk_pin);
    LOGI(TAG, "  MISO pin: %" PRId32, config->miso_pin);
    LOGI(TAG, "  MOSI pin: %" PRId32, config->mosi_pin);
    LOGI(TAG, "  Freq: %" PRId32 " MHz", config->max_freq_mhz);
    
    /* 分配句柄 */
    struct spi_drv_handle* h = (struct spi_drv_handle*)malloc(sizeof(struct spi_drv_handle));
    if (!h) {
        LOGE(TAG, "Failed to allocate handle");
        return SPI_DRV_ERR_HW;
    }
    memset(h, 0, sizeof(struct spi_drv_handle));
    
    /* 配置SPI总线 */
    spi_bus_config_t bus_config = {
        .mosi_io_num = (int)config->mosi_pin,
        .miso_io_num = (int)config->miso_pin,
        .sclk_io_num = (int)config->sclk_pin,
        .quadwp_io_num = -1,     /* 不使用Quad Write Protect */
        .quadhd_io_num = -1,     /* 不使用Quad Hold */
        .max_transfer_sz = 4096, /* 最大传输大小 */
    };
    
    /* 使用SPI2_HOST作为默认主机 */
    spi_host_device_t host = SPI2_HOST;
    
    esp_err_t ret = spi_bus_initialize(host, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        free(h);
        return SPI_DRV_ERR_HW;
    }
    
    /* 保存配置 */
    h->host = host;
    h->bus_config = bus_config;
    h->initialized = true;
    h->device_count = 0;
    
    *handle = h;
    
    LOGI(TAG, "SPI driver initialized successfully (host=%d)", host);
    
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
    
    /* 删除SPI总线 */
    esp_err_t ret = spi_bus_free(handle->host);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to free SPI bus: %s", esp_err_to_name(ret));
    }
    
    handle->initialized = false;
    free(handle);
    
    LOGI(TAG, "SPI driver deinitialized");
    
    return SPI_DRV_OK;
}

spi_drv_err_t spi_drv_add_device(spi_drv_handle_t handle, 
                                  int cs_pin, 
                                  int clock_speed_hz, 
                                  int mode, 
                                  spi_device_handle_t* dev_handle) {
    if (!handle || !dev_handle) {
        return SPI_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return SPI_DRV_ERR_NOT_INIT;
    }
    
    LOGI(TAG, "Adding SPI device: cs=%d, speed=%d Hz, mode=%d", 
         cs_pin, clock_speed_hz, mode);
    
    /* 配置SPI设备接口 */
    spi_device_interface_config_t dev_config = {
        .clock_speed_hz = clock_speed_hz,
        .mode = (uint8_t)mode,
        .spics_io_num = cs_pin,
        .queue_size = 1,
        .flags = 0,
        .address_bits = 0,
        .command_bits = 0,
        .dummy_bits = 0,
        .duty_cycle_pos = 128,    /* 50% duty cycle */
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .input_delay_ns = 0,
    };
    
    spi_device_handle_t device;
    esp_err_t ret = spi_bus_add_device(handle->host, &dev_config, &device);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return SPI_DRV_ERR_HW;
    }
    
    *dev_handle = device;
    handle->device_count++;
    
    LOGI(TAG, "SPI device added successfully (devices=%d)", handle->device_count);
    
    return SPI_DRV_OK;
}

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
    
    /* 配置传输事务 */
    spi_transaction_t trans = {
        .length = size * 8,          /* 以bit为单位 */
        .rxlength = size * 8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
        .flags = 0,
    };
    
    /* 执行传输 */
    esp_err_t ret = spi_device_polling_transmit(dev_handle, &trans);
    if (ret != ESP_OK) {
        LOGE(TAG, "SPI transfer failed: %s", esp_err_to_name(ret));
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

spi_drv_err_t spi_drv_write_cmd_data(spi_drv_handle_t handle,
                                       spi_device_handle_t dev_handle,
                                       int dc_pin,
                                       uint8_t cmd,
                                       const void* data,
                                       size_t data_len) {
    if (!handle || !data) {
        return SPI_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return SPI_DRV_ERR_NOT_INIT;
    }
    
    /* 拉低DC引脚表示发送命令 */
    if (dc_pin >= 0) {
        gpio_set_level(dc_pin, 0);
    }
    
    /* 发送命令 */
    spi_drv_err_t err = spi_drv_write(handle, dev_handle, &cmd, 1);
    if (err != SPI_DRV_OK) {
        return err;
    }
    
    /* 拉高DC引脚表示发送数据 */
    if (dc_pin >= 0) {
        gpio_set_level(dc_pin, 1);
    }
    
    /* 发送数据 */
    if (data_len > 0) {
        err = spi_drv_write(handle, dev_handle, data, data_len);
    }
    
    return err;
}
