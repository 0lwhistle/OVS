/**
 * @file uart_drv.c
 * @brief UART驱动实现
 * 
 * 实现UART串口的初始化、配置和数据传输功能。
 * 硬件参数从设备树读取，不在代码中硬编码。
 * 使用ESP-IDF UART驱动API。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "uart_drv.h"
#include "logger.h"

#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static const char* TAG = "[UART_DRV]";

/* ========================================================================== */
/*                              内部数据结构                                   */
/* ========================================================================== */

/**
 * @brief UART驱动句柄结构
 */
struct uart_drv_handle {
    uart_port_t port;             /**< UART端口号 */
    uart_config_t uart_config;    /**< UART配置 */
    QueueHandle_t event_queue;    /**< 事件队列 */
    bool initialized;             /**< 初始化标志 */
    int tx_pin;                   /**< TX引脚 */
    int rx_pin;                   /**< RX引脚 */
};

/* ========================================================================== */
/*                              内部函数                                       */
/* ========================================================================== */

/**
 * @brief 将配置中的奇偶校验字符串转换为ESP-IDF枚举
 */
static uart_parity_t parse_parity(const char* parity) {
    if (parity == NULL || strcmp(parity, "none") == 0) {
        return UART_PARITY_DISABLE;
    } else if (strcmp(parity, "even") == 0) {
        return UART_PARITY_EVEN;
    } else if (strcmp(parity, "odd") == 0) {
        return UART_PARITY_ODD;
    }
    return UART_PARITY_DISABLE;
}

/* ========================================================================== */
/*                              公共API实现                                    */
/* ========================================================================== */

uart_drv_err_t uart_drv_load_config(const char* path, uart_drv_config_t* config) {
    if (!path || !config) {
        return UART_DRV_ERR_PARAM;
    }
    
    dtree_err_t err;
    
    err = DTREE_INT(path, "tx_pin", &config->tx_pin);
    DTREE_CHECK_ERROR("Read tx_pin", err); if (err != DTREE_OK) {
        return UART_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT(path, "rx_pin", &config->rx_pin);
    DTREE_CHECK_ERROR("Read rx_pin", err); if (err != DTREE_OK) {
        return UART_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT(path, "baud_rate", &config->baud_rate);
    DTREE_CHECK_ERROR("Read baud_rate", err); if (err != DTREE_OK) {
        return UART_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT(path, "data_bits", &config->data_bits);
    DTREE_CHECK_ERROR("Read data_bits", err); if (err != DTREE_OK) {
        return UART_DRV_ERR_CONFIG;
    }
    
    err = DTREE_INT(path, "stop_bits", &config->stop_bits);
    DTREE_CHECK_ERROR("Read stop_bits", err); if (err != DTREE_OK) {
        return UART_DRV_ERR_CONFIG;
    }
    
    config->parity = NULL;
    err = DTREE_STR(path, "parity", &config->parity);
    DTREE_CHECK_ERROR("Read parity", err); if (err != DTREE_OK) {
        /* parity是可选的，默认为"none" */
        config->parity = "none";
    }
    
    LOGI(TAG, "UART config loaded from '%s': tx=%" PRId32 ", rx=%" PRId32 
         ", baud=%" PRId32 ", bits=%" PRId32 ", stop=%" PRId32 ", parity=%s",
         path, config->tx_pin, config->rx_pin, config->baud_rate, 
         config->data_bits, config->stop_bits, config->parity);
    
    return UART_DRV_OK;
}

uart_drv_err_t uart_drv_init(const uart_drv_config_t* config, uart_drv_handle_t* handle) {
    if (!config || !handle) {
        return UART_DRV_ERR_PARAM;
    }
    
    LOGI(TAG, "Initializing UART driver...");
    LOGI(TAG, "  TX pin: %" PRId32, config->tx_pin);
    LOGI(TAG, "  RX pin: %" PRId32, config->rx_pin);
    LOGI(TAG, "  Baud: %" PRId32, config->baud_rate);
    
    /* 分配句柄 */
    struct uart_drv_handle* h = (struct uart_drv_handle*)malloc(sizeof(struct uart_drv_handle));
    if (!h) {
        LOGE(TAG, "Failed to allocate handle");
        return UART_DRV_ERR_HW;
    }
    memset(h, 0, sizeof(struct uart_drv_handle));
    
    /* 配置UART参数 */
    uart_config_t uart_config = {
        .baud_rate = (int)config->baud_rate,
        .data_bits = (config->data_bits == 8) ? UART_DATA_8_BITS : UART_DATA_7_BITS,
        .parity = parse_parity(config->parity),
        .stop_bits = (config->stop_bits == 1) ? UART_STOP_BITS_1 : UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {
            .allow_pd = 0,
            .backup_before_sleep = 0,
        },
    };
    
    /* 使用UART1作为默认端口（UART0通常用于console） */
    uart_port_t port = UART_NUM_1;
    
    /* 安装UART驱动 */
    esp_err_t ret = uart_driver_install(port, 1024, 1024, 10, &h->event_queue, 0);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        free(h);
        return UART_DRV_ERR_HW;
    }
    
    /* 配置UART参数 */
    ret = uart_param_config(port, &uart_config);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
        uart_driver_delete(port);
        free(h);
        return UART_DRV_ERR_HW;
    }
    
    /* 设置引脚 */
    ret = uart_set_pin(port, (int)config->tx_pin, (int)config->rx_pin, -1, -1);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
        uart_driver_delete(port);
        free(h);
        return UART_DRV_ERR_HW;
    }
    
    /* 保存配置 */
    h->port = port;
    h->uart_config = uart_config;
    h->tx_pin = (int)config->tx_pin;
    h->rx_pin = (int)config->rx_pin;
    h->initialized = true;
    
    *handle = h;
    
    LOGI(TAG, "UART driver initialized successfully (port=%d)", port);
    
    return UART_DRV_OK;
}

uart_drv_err_t uart_drv_deinit(uart_drv_handle_t handle) {
    if (!handle) {
        return UART_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return UART_DRV_ERR_NOT_INIT;
    }
    
    LOGI(TAG, "Deinitializing UART driver...");
    
    /* 删除UART驱动 */
    esp_err_t ret = uart_driver_delete(handle->port);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to delete UART driver: %s", esp_err_to_name(ret));
    }
    
    handle->initialized = false;
    free(handle);
    
    LOGI(TAG, "UART driver deinitialized");
    
    return UART_DRV_OK;
}

uart_drv_err_t uart_drv_send(uart_drv_handle_t handle, const void* data, size_t size) {
    if (!handle || !data) {
        return UART_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return UART_DRV_ERR_NOT_INIT;
    }
    
    if (size == 0) {
        return UART_DRV_OK;
    }
    
    /* 发送数据 */
    int written = uart_write_bytes(handle->port, data, size);
    if (written < 0) {
        LOGE(TAG, "UART send failed");
        return UART_DRV_ERR_HW;
    }
    
    if ((size_t)written != size) {
        LOGW(TAG, "UART send incomplete: %d/%zu bytes", written, size);
        return UART_DRV_ERR_HW;
    }
    
    return UART_DRV_OK;
}

uart_drv_err_t uart_drv_receive(uart_drv_handle_t handle, void* buffer, size_t size, 
                                 size_t* bytes_read, uint32_t timeout_ms) {
    if (!handle || !buffer || !bytes_read) {
        return UART_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return UART_DRV_ERR_NOT_INIT;
    }
    
    if (size == 0) {
        *bytes_read = 0;
        return UART_DRV_OK;
    }
    
    /* 读取数据 */
    int read = uart_read_bytes(handle->port, buffer, size, pdMS_TO_TICKS(timeout_ms));
    if (read < 0) {
        LOGE(TAG, "UART receive failed");
        *bytes_read = 0;
        return UART_DRV_ERR_HW;
    }
    
    *bytes_read = (size_t)read;
    
    return UART_DRV_OK;
}

uart_drv_err_t uart_drv_flush(uart_drv_handle_t handle) {
    if (!handle) {
        return UART_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return UART_DRV_ERR_NOT_INIT;
    }
    
    /* 清空UART缓冲区 */
    esp_err_t ret = uart_flush(handle->port);
    if (ret != ESP_OK) {
        LOGE(TAG, "UART flush failed: %s", esp_err_to_name(ret));
        return UART_DRV_ERR_HW;
    }
    
    return UART_DRV_OK;
}
