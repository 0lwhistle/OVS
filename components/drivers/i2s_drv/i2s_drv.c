/**
 * @file i2s_drv.c
 * @brief I2S驱动实现
 * 
 * 实现I2S总线的初始化、配置和数据传输功能。
 * 硬件参数从设备树读取，不在代码中硬编码。
 * 使用ESP-IDF I2S标准模式API。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "i2s_drv.h"
#include "logger.h"

#include "driver/i2s_std.h"
#include "esp_err.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static const char* TAG = "[I2S_DRV]";

/* ========================================================================== */
/*                              内部数据结构                                   */
/* ========================================================================== */

/**
 * @brief I2S驱动句柄结构
 */
struct i2s_drv_handle {
    i2s_chan_handle_t rx_handle;    /**< I2S接收通道（麦克风） */
    i2s_chan_handle_t tx_handle;    /**< I2S发送通道（功放） */
    i2s_drv_config_t config;       /**< 配置信息 */
    bool initialized;              /**< 初始化标志 */
};

/* ========================================================================== */
/*                              公共API实现                                    */
/* ========================================================================== */

i2s_drv_err_t i2s_drv_load_config(i2s_drv_config_t* config) {
    if (!config) {
        return I2S_DRV_ERR_PARAM;
    }
    
    /* 从设备树读取I2S总线配置 */
    dtree_err_t err;
    
    err = DTREE_INT("i2s.bus", "bclk_pin", &config->bclk_pin);
    if (DTREE_CHECK_ERROR("Read bclk_pin", err)) {
        return I2S_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT("i2s.bus", "ws_pin", &config->ws_pin);
    if (DTREE_CHECK_ERROR("Read ws_pin", err)) {
        return I2S_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT("i2s.microphone", "data_in_pin", &config->data_in_pin);
    if (DTREE_CHECK_ERROR("Read data_in_pin", err)) {
        return I2S_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT("i2s.amplifier", "data_out_pin", &config->data_out_pin);
    if (DTREE_CHECK_ERROR("Read data_out_pin", err)) {
        return I2S_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT("i2s.bus", "sample_rate_hz", &config->sample_rate);
    if (DTREE_CHECK_ERROR("Read sample_rate_hz", err)) {
        return I2S_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT("i2s.bus", "data_bits", &config->data_bits);
    if (DTREE_CHECK_ERROR("Read data_bits", err)) {
        return I2S_DRV_ERR_CONFIG;
    }
    
    /* 读取通道模式 */
    const char* channel_str = NULL;
    err = DTREE_STR("i2s.bus", "channel", &channel_str);
    if (DTREE_CHECK_ERROR("Read channel", err)) {
        return I2S_DRV_ERR_CONFIG;
    }
    config->is_mono = (strcmp(channel_str, "mono") == 0);
    
    LOGI(TAG, "I2S config loaded: bclk=%" PRId32 ", ws=%" PRId32 
         ", din=%" PRId32 ", dout=%" PRId32 
         ", rate=%" PRId32 ", bits=%" PRId32 ", mono=%d",
         config->bclk_pin, config->ws_pin, 
         config->data_in_pin, config->data_out_pin,
         config->sample_rate, config->data_bits, config->is_mono);
    
    return I2S_DRV_OK;
}

i2s_drv_err_t i2s_drv_init(const i2s_drv_config_t* config, i2s_drv_handle_t* handle) {
    if (!config || !handle) {
        return I2S_DRV_ERR_PARAM;
    }
    
    LOGI(TAG, "Initializing I2S driver...");
    LOGI(TAG, "  BCLK pin: %" PRId32, config->bclk_pin);
    LOGI(TAG, "  WS pin: %" PRId32, config->ws_pin);
    LOGI(TAG, "  DIN pin: %" PRId32, config->data_in_pin);
    LOGI(TAG, "  DOUT pin: %" PRId32, config->data_out_pin);
    LOGI(TAG, "  Sample rate: %" PRId32 " Hz", config->sample_rate);
    LOGI(TAG, "  Data bits: %" PRId32, config->data_bits);
    
    /* 分配句柄 */
    struct i2s_drv_handle* h = (struct i2s_drv_handle*)malloc(sizeof(struct i2s_drv_handle));
    if (!h) {
        LOGE(TAG, "Failed to allocate handle");
        return I2S_DRV_ERR_HW;
    }
    memset(h, 0, sizeof(struct i2s_drv_handle));
    
    /* 保存配置 */
    h->config = *config;
    
    /* 确定数据位宽 */
    i2s_data_bit_width_t bits;
    switch (config->data_bits) {
        case 8:  bits = I2S_DATA_BIT_WIDTH_8BIT; break;
        case 16: bits = I2S_DATA_BIT_WIDTH_16BIT; break;
        case 24: bits = I2S_DATA_BIT_WIDTH_24BIT; break;
        case 32: bits = I2S_DATA_BIT_WIDTH_32BIT; break;
        default:
            LOGE(TAG, "Unsupported data bits: %" PRId32, config->data_bits);
            free(h);
            return I2S_DRV_ERR_PARAM;
    }
    
    /* 确定通道模式 */
    i2s_slot_mode_t slot_mode = config->is_mono ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
    
    /* ==================== 初始化I2S通道 ==================== */
    
    /* 配置I2S通道 */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 240;
    
    /* 创建I2S通道（全双工模式） */
    esp_err_t ret = i2s_new_channel(&chan_cfg, &h->tx_handle, &h->rx_handle);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to create I2S channels: %s", esp_err_to_name(ret));
        free(h);
        return I2S_DRV_ERR_HW;
    }
    
    /* 配置I2S标准模式 */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)config->sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, slot_mode),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)config->bclk_pin,
            .ws = (gpio_num_t)config->ws_pin,
            .dout = (gpio_num_t)config->data_out_pin,
            .din = (gpio_num_t)config->data_in_pin,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    /* 初始化TX通道（功放） */
    ret = i2s_channel_init_std_mode(h->tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to init TX channel: %s", esp_err_to_name(ret));
        i2s_del_channel(h->tx_handle);
        i2s_del_channel(h->rx_handle);
        free(h);
        return I2S_DRV_ERR_HW;
    }
    
    /* 初始化RX通道（麦克风） */
    ret = i2s_channel_init_std_mode(h->rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to init RX channel: %s", esp_err_to_name(ret));
        i2s_del_channel(h->tx_handle);
        i2s_del_channel(h->rx_handle);
        free(h);
        return I2S_DRV_ERR_HW;
    }
    
    /* 使能通道 */
    ret = i2s_channel_enable(h->tx_handle);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to enable TX channel: %s", esp_err_to_name(ret));
        i2s_del_channel(h->tx_handle);
        i2s_del_channel(h->rx_handle);
        free(h);
        return I2S_DRV_ERR_HW;
    }
    
    ret = i2s_channel_enable(h->rx_handle);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to enable RX channel: %s", esp_err_to_name(ret));
        i2s_channel_disable(h->tx_handle);
        i2s_del_channel(h->tx_handle);
        i2s_del_channel(h->rx_handle);
        free(h);
        return I2S_DRV_ERR_HW;
    }
    
    h->initialized = true;
    *handle = h;
    
    LOGI(TAG, "I2S driver initialized successfully");
    
    return I2S_DRV_OK;
}

i2s_drv_err_t i2s_drv_deinit(i2s_drv_handle_t handle) {
    if (!handle) {
        return I2S_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return I2S_DRV_ERR_NOT_INIT;
    }
    
    LOGI(TAG, "Deinitializing I2S driver...");
    
    /* 禁用通道 */
    if (handle->tx_handle) {
        i2s_channel_disable(handle->tx_handle);
        i2s_del_channel(handle->tx_handle);
    }
    
    if (handle->rx_handle) {
        i2s_channel_disable(handle->rx_handle);
        i2s_del_channel(handle->rx_handle);
    }
    
    handle->initialized = false;
    free(handle);
    
    LOGI(TAG, "I2S driver deinitialized");
    
    return I2S_DRV_OK;
}

i2s_drv_err_t i2s_drv_read(i2s_drv_handle_t handle, void* buffer, size_t size, size_t* bytes_read) {
    if (!handle || !buffer || !bytes_read) {
        return I2S_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return I2S_DRV_ERR_NOT_INIT;
    }
    
    if (size == 0) {
        *bytes_read = 0;
        return I2S_DRV_OK;
    }
    
    /* 从麦克风读取数据 */
    esp_err_t ret = i2s_channel_read(handle->rx_handle, buffer, size, bytes_read, 1000);
    if (ret != ESP_OK) {
        LOGE(TAG, "I2S read failed: %s", esp_err_to_name(ret));
        *bytes_read = 0;
        return I2S_DRV_ERR_HW;
    }
    
    return I2S_DRV_OK;
}

i2s_drv_err_t i2s_drv_write(i2s_drv_handle_t handle, const void* buffer, size_t size, size_t* bytes_written) {
    if (!handle || !buffer || !bytes_written) {
        return I2S_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return I2S_DRV_ERR_NOT_INIT;
    }
    
    if (size == 0) {
        *bytes_written = 0;
        return I2S_DRV_OK;
    }
    
    /* 向功放写入数据 */
    esp_err_t ret = i2s_channel_write(handle->tx_handle, buffer, size, bytes_written, 1000);
    if (ret != ESP_OK) {
        LOGE(TAG, "I2S write failed: %s", esp_err_to_name(ret));
        *bytes_written = 0;
        return I2S_DRV_ERR_HW;
    }
    
    return I2S_DRV_OK;
}
