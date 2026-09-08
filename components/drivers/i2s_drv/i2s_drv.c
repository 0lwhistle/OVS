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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

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
    uint32_t ref_count;            /**< 引用计数（共享计数模型） */
};

/* ========================================================================== */
/*                    共享总线注册表（引用计数模型）                            */
/* ========================================================================== */

#define I2S_DRV_MAX_PORTS 2
static struct i2s_drv_handle* s_i2s_buses[I2S_DRV_MAX_PORTS] = { NULL };
static SemaphoreHandle_t s_i2s_bus_lock = NULL;

static int i2s_port_to_slot(int32_t port) {
    if (port == 0 || port == 1) {
        return (int)port;
    }
    return -1;
}

static bool i2s_bus_lock_take(void) {
    if (!s_i2s_bus_lock) {
        s_i2s_bus_lock = xSemaphoreCreateMutex();
    }
    if (!s_i2s_bus_lock) {
        LOGE(TAG, "Failed to create I2S bus lock");
        return false;
    }
    xSemaphoreTake(s_i2s_bus_lock, portMAX_DELAY);
    return true;
}

static void i2s_bus_lock_give(void) {
    if (s_i2s_bus_lock) {
        xSemaphoreGive(s_i2s_bus_lock);
    }
}

static bool i2s_drv_config_equal(const i2s_drv_config_t* a, const i2s_drv_config_t* b) {
    return a && b
        && a->port == b->port
        && a->bclk_pin == b->bclk_pin
        && a->ws_pin == b->ws_pin
        && a->data_in_pin == b->data_in_pin
        && a->data_out_pin == b->data_out_pin
        && a->sample_rate == b->sample_rate
        && a->data_bits == b->data_bits
        && a->is_mono == b->is_mono;
}

static i2s_drv_err_t i2s_bus_destroy(int slot) {
    if (slot < 0 || slot >= I2S_DRV_MAX_PORTS) {
        return I2S_DRV_ERR_PARAM;
    }

    struct i2s_drv_handle* h = s_i2s_buses[slot];
    if (!h) {
        return I2S_DRV_OK;
    }

    if (h->tx_handle) {
        i2s_channel_disable(h->tx_handle);
        i2s_del_channel(h->tx_handle);
        h->tx_handle = NULL;
    }

    if (h->rx_handle) {
        i2s_channel_disable(h->rx_handle);
        i2s_del_channel(h->rx_handle);
        h->rx_handle = NULL;
    }

    free(h);
    s_i2s_buses[slot] = NULL;
    return I2S_DRV_OK;
}

/* ========================================================================== */
/*                              公共API实现                                    */
/* ========================================================================== */

i2s_drv_err_t i2s_drv_load_config(dtree_node_t* bus_node, i2s_drv_config_t* config) {
    if (!bus_node || !config) {
        return I2S_DRV_ERR_PARAM;
    }

    /* 总线节点 compatible 校验，防止把错误的节点当总线 */
    const char* compat = dtree_get_compatible(bus_node);
    if (!compat || strcmp(compat, "esp32s3-i2s") != 0) {
        LOGE(TAG, "Bus node compatible '%s' is not 'esp32s3-i2s'",
             compat ? compat : "null");
        return I2S_DRV_ERR_CONFIG;
    }

    /* 从设备树读取I2S总线配置 */
    dtree_err_t err;

    /* 控制器编号来自节点名，例如 "i2s0"、"i2s1" */
    err = dtree_get_host_id(bus_node, "i2s", &config->port);
    DTREE_CHECK_ERROR("Read i2s host id", err);
    if (err != DTREE_OK || i2s_port_to_slot(config->port) < 0) {
        LOGE(TAG, "Unsupported I2S port id: %" PRId32, config->port);
        return I2S_DRV_ERR_CONFIG;
    }

    err = dtree_get_int(bus_node, "bclk_pin", &config->bclk_pin);
    DTREE_CHECK_ERROR("Read bclk_pin", err); if (err != DTREE_OK) {
        return I2S_DRV_ERR_CONFIG;
    }

    err = dtree_get_int(bus_node, "ws_pin", &config->ws_pin);
    DTREE_CHECK_ERROR("Read ws_pin", err); if (err != DTREE_OK) {
        return I2S_DRV_ERR_CONFIG;
    }

    /* DIN/DOUT 属于设备（麦克风/功放），由调用方按 compatible
     * 查设备节点后填充；总线驱动不关心具体设备 */
    config->data_in_pin = -1;
    config->data_out_pin = -1;

    err = dtree_get_int(bus_node, "sample_rate_hz", &config->sample_rate);
    DTREE_CHECK_ERROR("Read sample_rate_hz", err); if (err != DTREE_OK) {
        return I2S_DRV_ERR_CONFIG;
    }

    err = dtree_get_int(bus_node, "data_bits", &config->data_bits);
    DTREE_CHECK_ERROR("Read data_bits", err); if (err != DTREE_OK) {
        return I2S_DRV_ERR_CONFIG;
    }

    /* 读取通道模式 */
    const char* channel_str = NULL;
    err = dtree_get_string(bus_node, "channel", &channel_str);
    DTREE_CHECK_ERROR("Read channel", err); if (err != DTREE_OK) {
        return I2S_DRV_ERR_CONFIG;
    }
    config->is_mono = (strcmp(channel_str, "mono") == 0);

    LOGI(TAG, "I2S config loaded: port=i2s%" PRId32 ", bclk=%" PRId32 ", ws=%" PRId32
         ", rate=%" PRId32 ", bits=%" PRId32 ", mono=%d",
         config->port, config->bclk_pin, config->ws_pin,
         config->sample_rate, config->data_bits, config->is_mono);

    return I2S_DRV_OK;
}

i2s_drv_err_t i2s_drv_init(const i2s_drv_config_t* config, i2s_drv_handle_t* handle) {
    if (!config || !handle) {
        return I2S_DRV_ERR_PARAM;
    }

    if (!i2s_bus_lock_take()) {
        return I2S_DRV_ERR_HW;
    }

    int slot = i2s_port_to_slot(config->port);
    if (slot < 0) {
        LOGE(TAG, "Unsupported I2S port id: %" PRId32, config->port);
        i2s_bus_lock_give();
        return I2S_DRV_ERR_CONFIG;
    }

    /* 对应 port 的总线已存在：配置一致则共享句柄（引用计数 +1） */
    struct i2s_drv_handle* existing = s_i2s_buses[slot];
    if (existing) {
        if (!i2s_drv_config_equal(config, &existing->config)) {
            LOGE(TAG, "I2S port i2s%" PRId32 " already initialized with different config",
                 config->port);
            i2s_bus_lock_give();
            return I2S_DRV_ERR_CONFIG;
        }

        existing->ref_count++;
        *handle = existing;
        LOGI(TAG, "I2S port i2s%" PRId32 " shared: ref=%" PRIu32,
             config->port, existing->ref_count);
        i2s_bus_lock_give();
        return I2S_DRV_OK;
    }

    LOGI(TAG, "Initializing I2S driver (port i2s%" PRId32 ", first user)...", config->port);
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
        i2s_bus_lock_give();
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
            i2s_bus_lock_give();
            return I2S_DRV_ERR_PARAM;
    }
    
    /* 确定通道模式 */
    i2s_slot_mode_t slot_mode = config->is_mono ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
    
    /* ==================== 初始化I2S通道 ==================== */
    
    /* 配置I2S通道 */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(config->port,
                                                            I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 240;
    
    /* 创建I2S通道（全双工模式） */
    esp_err_t ret = i2s_new_channel(&chan_cfg, &h->tx_handle, &h->rx_handle);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to create I2S channels: %s", esp_err_to_name(ret));
        free(h);
        i2s_bus_lock_give();
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
        i2s_bus_lock_give();
        return I2S_DRV_ERR_HW;
    }
    
    /* 初始化RX通道（麦克风） */
    ret = i2s_channel_init_std_mode(h->rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to init RX channel: %s", esp_err_to_name(ret));
        i2s_del_channel(h->tx_handle);
        i2s_del_channel(h->rx_handle);
        free(h);
        i2s_bus_lock_give();
        return I2S_DRV_ERR_HW;
    }
    
    /* 使能通道 */
    ret = i2s_channel_enable(h->tx_handle);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to enable TX channel: %s", esp_err_to_name(ret));
        i2s_del_channel(h->tx_handle);
        i2s_del_channel(h->rx_handle);
        free(h);
        i2s_bus_lock_give();
        return I2S_DRV_ERR_HW;
    }
    
    ret = i2s_channel_enable(h->rx_handle);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to enable RX channel: %s", esp_err_to_name(ret));
        i2s_channel_disable(h->tx_handle);
        i2s_del_channel(h->tx_handle);
        i2s_del_channel(h->rx_handle);
        free(h);
        i2s_bus_lock_give();
        return I2S_DRV_ERR_HW;
    }

    h->initialized = true;
    h->ref_count = 1;
    s_i2s_buses[slot] = h;
    *handle = h;

    LOGI(TAG, "I2S driver initialized successfully (port=i2s%" PRId32 ", ref=1)",
         config->port);
    i2s_bus_lock_give();
    return I2S_DRV_OK;
}

i2s_drv_err_t i2s_drv_deinit(i2s_drv_handle_t handle) {
    if (!handle) {
        return I2S_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return I2S_DRV_ERR_NOT_INIT;
    }

    if (!i2s_bus_lock_take()) {
        return I2S_DRV_ERR_HW;
    }

    int slot = -1;
    for (int i = 0; i < I2S_DRV_MAX_PORTS; i++) {
        if (s_i2s_buses[i] == handle) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        LOGE(TAG, "Handle is not owned by I2S driver registry");
        i2s_bus_lock_give();
        return I2S_DRV_ERR_PARAM;
    }

    if (handle->ref_count == 0) {
        LOGW(TAG, "I2S bus already fully released");
        i2s_bus_lock_give();
        return I2S_DRV_OK;
    }

    if (handle->ref_count > 1) {
        handle->ref_count--;
        LOGI(TAG, "I2S driver released one reference (remaining=%" PRIu32 ")",
             handle->ref_count);
        i2s_bus_lock_give();
        return I2S_DRV_OK;
    }

    LOGI(TAG, "Deinitializing I2S driver (last reference)...");
    i2s_drv_err_t err = i2s_bus_destroy(slot);
    i2s_bus_lock_give();
    return err;
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
