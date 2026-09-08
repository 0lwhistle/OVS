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
#include "freertos/semphr.h"
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
    uart_drv_config_t drv_config; /**< 用户配置（共享一致性校验） */
    QueueHandle_t event_queue;    /**< 事件队列 */
    bool initialized;             /**< 初始化标志 */
    int tx_pin;                   /**< TX引脚 */
    int rx_pin;                   /**< RX引脚 */
    uint32_t ref_count;           /**< 引用计数（共享计数模型） */
};

/* ========================================================================== */
/*                    共享总线注册表（引用计数模型）                            */
/* ========================================================================== */

#define UART_DRV_MAX_PORTS 3
static struct uart_drv_handle* s_uart_buses[UART_DRV_MAX_PORTS] = { NULL };
static SemaphoreHandle_t s_uart_bus_lock = NULL;

static int uart_port_to_slot(int32_t port) {
    if (port >= 0 && port < UART_DRV_MAX_PORTS) {
        return (int)port;
    }
    return -1;
}

static bool uart_bus_lock_take(void) {
    if (!s_uart_bus_lock) {
        s_uart_bus_lock = xSemaphoreCreateMutex();
    }
    if (!s_uart_bus_lock) {
        LOGE(TAG, "Failed to create UART bus lock");
        return false;
    }
    xSemaphoreTake(s_uart_bus_lock, portMAX_DELAY);
    return true;
}

static void uart_bus_lock_give(void) {
    if (s_uart_bus_lock) {
        xSemaphoreGive(s_uart_bus_lock);
    }
}

static bool uart_drv_config_equal(const uart_drv_config_t* a, const uart_drv_config_t* b) {
    return a && b
        && a->port == b->port
        && a->tx_pin == b->tx_pin
        && a->rx_pin == b->rx_pin
        && a->baud_rate == b->baud_rate
        && a->data_bits == b->data_bits
        && a->stop_bits == b->stop_bits
        && ((a->parity == NULL && b->parity == NULL)
            || (a->parity && b->parity && strcmp(a->parity, b->parity) == 0));
}

static uart_drv_err_t uart_bus_destroy(int slot) {
    if (slot < 0 || slot >= UART_DRV_MAX_PORTS) {
        return UART_DRV_ERR_PARAM;
    }

    struct uart_drv_handle* h = s_uart_buses[slot];
    if (!h) {
        return UART_DRV_OK;
    }

    esp_err_t ret = uart_driver_delete(h->port);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to delete UART driver: %s", esp_err_to_name(ret));
    }

    free(h);
    s_uart_buses[slot] = NULL;
    return UART_DRV_OK;
}

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

uart_drv_err_t uart_drv_load_config(dtree_node_t* bus_node, uart_drv_config_t* config) {
    if (!bus_node || !config) {
        return UART_DRV_ERR_PARAM;
    }

    /* 总线节点 compatible 校验，防止把错误的节点当总线 */
    const char* compat = dtree_get_compatible(bus_node);
    if (!compat || strcmp(compat, "esp32s3-uart") != 0) {
        LOGE(TAG, "Bus node compatible '%s' is not 'esp32s3-uart'",
             compat ? compat : "null");
        return UART_DRV_ERR_CONFIG;
    }

    dtree_err_t err;

    /* 控制器编号来自节点名，例如 "uart0"、"uart1"、"uart2" */
    err = dtree_get_host_id(bus_node, "uart", &config->port);
    DTREE_CHECK_ERROR("Read uart host id", err);
    if (err != DTREE_OK || uart_port_to_slot(config->port) < 0) {
        LOGE(TAG, "Unsupported UART port id: %" PRId32, config->port);
        return UART_DRV_ERR_CONFIG;
    }

    err = dtree_get_int(bus_node, "tx_pin", &config->tx_pin);
    DTREE_CHECK_ERROR("Read tx_pin", err); if (err != DTREE_OK) {
        return UART_DRV_ERR_CONFIG;
    }

    err = dtree_get_int(bus_node, "rx_pin", &config->rx_pin);
    DTREE_CHECK_ERROR("Read rx_pin", err); if (err != DTREE_OK) {
        return UART_DRV_ERR_CONFIG;
    }

    err = dtree_get_int(bus_node, "baud_rate", &config->baud_rate);
    DTREE_CHECK_ERROR("Read baud_rate", err); if (err != DTREE_OK) {
        return UART_DRV_ERR_CONFIG;
    }

    err = dtree_get_int(bus_node, "data_bits", &config->data_bits);
    DTREE_CHECK_ERROR("Read data_bits", err); if (err != DTREE_OK) {
        return UART_DRV_ERR_CONFIG;
    }

    err = dtree_get_int(bus_node, "stop_bits", &config->stop_bits);
    DTREE_CHECK_ERROR("Read stop_bits", err); if (err != DTREE_OK) {
        return UART_DRV_ERR_CONFIG;
    }

    config->parity = NULL;
    err = dtree_get_string(bus_node, "parity", &config->parity);
    DTREE_CHECK_ERROR("Read parity", err); if (err != DTREE_OK) {
        /* parity是可选的，默认为"none" */
        config->parity = "none";
    }

    LOGI(TAG, "UART config loaded from '%s': port=uart%" PRId32 ", tx=%" PRId32
         ", rx=%" PRId32 ", baud=%" PRId32 ", bits=%" PRId32 ", stop=%" PRId32
         ", parity=%s",
         dtree_get_node_name(bus_node) ? dtree_get_node_name(bus_node) : "?",
         config->port, config->tx_pin, config->rx_pin, config->baud_rate,
         config->data_bits, config->stop_bits, config->parity);

    return UART_DRV_OK;
}

uart_drv_err_t uart_drv_init(const uart_drv_config_t* config, uart_drv_handle_t* handle) {
    if (!config || !handle) {
        return UART_DRV_ERR_PARAM;
    }

    if (!uart_bus_lock_take()) {
        return UART_DRV_ERR_HW;
    }

    int slot = uart_port_to_slot(config->port);
    if (slot < 0) {
        LOGE(TAG, "Unsupported UART port id: %" PRId32, config->port);
        uart_bus_lock_give();
        return UART_DRV_ERR_CONFIG;
    }

    /* 对应 port 的总线已存在：配置一致则共享句柄（引用计数 +1） */
    struct uart_drv_handle* existing = s_uart_buses[slot];
    if (existing) {
        if (!uart_drv_config_equal(config, &existing->drv_config)) {
            LOGE(TAG, "UART port uart%" PRId32 " already initialized with different config",
                 config->port);
            uart_bus_lock_give();
            return UART_DRV_ERR_CONFIG;
        }

        existing->ref_count++;
        *handle = existing;
        LOGI(TAG, "UART port uart%" PRId32 " shared: ref=%" PRIu32,
             config->port, existing->ref_count);
        uart_bus_lock_give();
        return UART_DRV_OK;
    }

    LOGI(TAG, "Initializing UART driver (port uart%" PRId32 ", first user)...", config->port);
    LOGI(TAG, "  TX pin: %" PRId32, config->tx_pin);
    LOGI(TAG, "  RX pin: %" PRId32, config->rx_pin);
    LOGI(TAG, "  Baud: %" PRId32, config->baud_rate);
    
    /* 分配句柄 */
    struct uart_drv_handle* h = (struct uart_drv_handle*)malloc(sizeof(struct uart_drv_handle));
    if (!h) {
        LOGE(TAG, "Failed to allocate handle");
        uart_bus_lock_give();
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
    
    uart_port_t port = (uart_port_t)config->port;
    
    /* 安装UART驱动 */
    esp_err_t ret = uart_driver_install(port, 1024, 1024, 10, &h->event_queue, 0);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        free(h);
        uart_bus_lock_give();
        return UART_DRV_ERR_HW;
    }
    
    /* 配置UART参数 */
    ret = uart_param_config(port, &uart_config);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
        uart_driver_delete(port);
        free(h);
        uart_bus_lock_give();
        return UART_DRV_ERR_HW;
    }
    
    /* 设置引脚 */
    ret = uart_set_pin(port, (int)config->tx_pin, (int)config->rx_pin, -1, -1);
    if (ret != ESP_OK) {
        LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
        uart_driver_delete(port);
        free(h);
        uart_bus_lock_give();
        return UART_DRV_ERR_HW;
    }

    /* 保存配置 */
    h->port = port;
    h->uart_config = uart_config;
    h->drv_config = *config;
    h->tx_pin = (int)config->tx_pin;
    h->rx_pin = (int)config->rx_pin;
    h->initialized = true;
    h->ref_count = 1;

    s_uart_buses[slot] = h;
    *handle = h;

    LOGI(TAG, "UART driver initialized successfully (port=uart%" PRId32 ", ref=1)",
         config->port);
    uart_bus_lock_give();
    return UART_DRV_OK;
}

uart_drv_err_t uart_drv_deinit(uart_drv_handle_t handle) {
    if (!handle) {
        return UART_DRV_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return UART_DRV_ERR_NOT_INIT;
    }

    if (!uart_bus_lock_take()) {
        return UART_DRV_ERR_HW;
    }

    int slot = -1;
    for (int i = 0; i < UART_DRV_MAX_PORTS; i++) {
        if (s_uart_buses[i] == handle) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        LOGE(TAG, "Handle is not owned by UART driver registry");
        uart_bus_lock_give();
        return UART_DRV_ERR_PARAM;
    }

    if (handle->ref_count == 0) {
        LOGW(TAG, "UART bus already fully released");
        uart_bus_lock_give();
        return UART_DRV_OK;
    }

    if (handle->ref_count > 1) {
        handle->ref_count--;
        LOGI(TAG, "UART driver released one reference (remaining=%" PRIu32 ")",
             handle->ref_count);
        uart_bus_lock_give();
        return UART_DRV_OK;
    }

    LOGI(TAG, "Deinitializing UART driver (last reference)...");
    uart_drv_err_t err = uart_bus_destroy(slot);
    uart_bus_lock_give();
    return err;
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
