/**
 * @file lora.c
 * @brief LoRa无线通信模块实现
 * 
 * 实现LoRa模块的初始化、发送、接收等功能。
 * 使用UART总线通信，通过event_bus发布无线数据事件。
 * 使用tasker进行周期性接收轮询。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "lora.h"
#include "uart_drv.h"
#include "event_bus.h"
#include "tasker.h"
#include "logger.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static const char* TAG = "[LORA]";

/* ========================================================================== */
/*                              常量定义                                       */
/* ========================================================================== */

/** LoRa接收缓冲区大小 */
#define LORA_RX_BUFFER_SIZE     256

/** LoRa最大数据包大小 */
#define LORA_MAX_PACKET_SIZE    200

/* ========================================================================== */
/*                              内部变量                                       */
/* ========================================================================== */

/** UART驱动句柄 */
static uart_drv_handle_t s_uart_handle = NULL;

/** M0控制引脚 */
static int s_m0_pin = -1;

/** M1控制引脚 */
static int s_m1_pin = -1;

/** AUX状态引脚 */
static int s_aux_pin = -1;

/** 初始化标志 */
static bool s_initialized = false;

/** 接收回调 */
static lora_rx_cb_t s_rx_callback = NULL;

/** 接收回调用户数据 */
static void* s_rx_user_data = NULL;

/** 接收缓冲区 */
static uint8_t s_rx_buffer[LORA_RX_BUFFER_SIZE];

/** 周期任务节点 */
static struct task_node s_periodic_task;

/** 周期任务运行标志 */
static bool s_periodic_running = false;

/* ========================================================================== */
/*                              内部函数                                       */
/* ========================================================================== */

/**
 * @brief 初始化GPIO控制引脚
 */
static lora_err_t lora_init_gpio(void) {
    dtree_err_t err;
    int32_t m0_pin, m1_pin, aux_pin;
    
    err = DTREE_INT("lora.control", "m0_pin", &m0_pin);
    DTREE_CHECK_ERROR("Read m0_pin", err); if (err != DTREE_OK) {
        return LORA_ERR_UART;
    }
    s_m0_pin = (int)m0_pin;
    
    err = DTREE_INT("lora.control", "m1_pin", &m1_pin);
    DTREE_CHECK_ERROR("Read m1_pin", err); if (err != DTREE_OK) {
        return LORA_ERR_UART;
    }
    s_m1_pin = (int)m1_pin;
    
    err = DTREE_INT("lora.control", "aux_pin", &aux_pin);
    DTREE_CHECK_ERROR("Read aux_pin", err); if (err != DTREE_OK) {
        return LORA_ERR_UART;
    }
    s_aux_pin = (int)aux_pin;
    
    /* 配置GPIO */
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    
    /* M0和M1为输出 */
    io_conf.pin_bit_mask = (1ULL << s_m0_pin) | (1ULL << s_m1_pin);
    gpio_config(&io_conf);
    
    /* AUX为输入 */
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << s_aux_pin);
    gpio_config(&io_conf);
    
    /* 设置为普通模式 (M0=0, M1=0) */
    gpio_set_level(s_m0_pin, 0);
    gpio_set_level(s_m1_pin, 0);
    
    LOGI(TAG, "GPIO initialized: M0=%d, M1=%d, AUX=%d", 
         s_m0_pin, s_m1_pin, s_aux_pin);
    
    return LORA_OK;
}

/**
 * @brief 等待AUX引脚就绪
 */
static lora_err_t lora_wait_aux(uint32_t timeout_ms) {
    uint32_t start = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    
    while (gpio_get_level(s_aux_pin) == 0) {
        if ((xTaskGetTickCount() - start) > timeout_ticks) {
            LOGE(TAG, "Timeout waiting for AUX");
            return LORA_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    return LORA_OK;
}

/**
 * @brief 周期性接收任务回调
 */
static enum task_t lora_periodic_task_fn(void* ctx) {
    (void)ctx;
    
    size_t bytes_read = 0;
    lora_err_t err = lora_receive(s_rx_buffer, LORA_RX_BUFFER_SIZE, &bytes_read);
    
    if (err == LORA_OK && bytes_read > 0) {
        LOGD(TAG, "Received %zu bytes", bytes_read);
        
        /* 调用回调 */
        if (s_rx_callback) {
            s_rx_callback(s_rx_buffer, bytes_read, 0, s_rx_user_data);
        }
        
        /* 发布事件 */
        event_lora_data_t event_data = {
            .data = s_rx_buffer,
            .length = bytes_read,
        };
        EVENT_BUS_PUBLISH(EVENT_LORA_DATA_RECEIVED, &event_data);
    }
    
    return TASK_OK;
}

/* ========================================================================== */
/*                              公共API实现                                    */
/* ========================================================================== */

lora_err_t lora_init(void) {
    if (s_initialized) {
        LOGW(TAG, "Already initialized");
        return LORA_OK;
    }
    
    LOGI(TAG, "Initializing LoRa module...");
    
    /* 初始化GPIO */
    lora_err_t err = lora_init_gpio();
    if (err != LORA_OK) {
        LOGE(TAG, "GPIO init failed");
        return err;
    }
    
    /* 加载UART配置 */
    uart_drv_config_t uart_config;
    uart_drv_err_t uart_err = uart_drv_load_config("lora.uart", &uart_config);
    if (uart_err != UART_DRV_OK) {
        LOGE(TAG, "Failed to load UART config: %d", uart_err);
        return LORA_ERR_UART;
    }
    
    /* 初始化UART驱动 */
    uart_err = uart_drv_init(&uart_config, &s_uart_handle);
    if (uart_err != UART_DRV_OK) {
        LOGE(TAG, "Failed to init UART: %d", uart_err);
        return LORA_ERR_UART;
    }
    
    /* 等待模块就绪 */
    vTaskDelay(pdMS_TO_TICKS(100));
    lora_wait_aux(1000);
    
    s_initialized = true;
    
    LOGI(TAG, "LoRa module initialized successfully");
    
    /* 发布LoRa就绪事件 */
    EVENT_BUS_PUBLISH_EMPTY(EVENT_LORA_READY);
    
    return LORA_OK;
}

lora_err_t lora_deinit(void) {
    if (!s_initialized) {
        return LORA_OK;
    }
    
    LOGI(TAG, "Deinitializing LoRa module...");
    
    /* 停止周期任务 */
    lora_stop_receive_task();
    
    /* 反初始化UART */
    if (s_uart_handle) {
        uart_drv_deinit(s_uart_handle);
        s_uart_handle = NULL;
    }
    
    s_initialized = false;
    
    LOGI(TAG, "LoRa module deinitialized");
    
    return LORA_OK;
}

lora_err_t lora_send(const void* data, size_t size) {
    if (!data) {
        return LORA_ERR_PARAM;
    }
    
    if (!s_initialized) {
        return LORA_ERR_NOT_INIT;
    }
    
    if (size > LORA_MAX_PACKET_SIZE) {
        LOGE(TAG, "Data too large: %zu > %d", size, LORA_MAX_PACKET_SIZE);
        return LORA_ERR_PARAM;
    }
    
    /* 等待模块就绪 */
    lora_err_t err = lora_wait_aux(1000);
    if (err != LORA_OK) {
        return err;
    }
    
    /* 发送数据 */
    uart_drv_err_t uart_err = uart_drv_send(s_uart_handle, data, size);
    if (uart_err != UART_DRV_OK) {
        LOGE(TAG, "Send failed: %d", uart_err);
        return LORA_ERR_UART;
    }
    
    /* 等待发送完成 */
    lora_wait_aux(2000);
    
    LOGD(TAG, "Sent %zu bytes", size);
    
    return LORA_OK;
}

lora_err_t lora_receive(void* buffer, size_t size, size_t* bytes_read) {
    if (!buffer || !bytes_read) {
        return LORA_ERR_PARAM;
    }
    
    if (!s_initialized) {
        return LORA_ERR_NOT_INIT;
    }
    
    /* 非阻塞读取 */
    uart_drv_err_t uart_err = uart_drv_receive(s_uart_handle, buffer, size, bytes_read, 0);
    if (uart_err != UART_DRV_OK) {
        *bytes_read = 0;
        return LORA_ERR_UART;
    }
    
    if (*bytes_read == 0) {
        return LORA_ERR_NO_DATA;
    }
    
    return LORA_OK;
}

lora_err_t lora_register_rx_callback(lora_rx_cb_t callback, void* user_data) {
    s_rx_callback = callback;
    s_rx_user_data = user_data;
    
    LOGI(TAG, "RX callback registered");
    
    return LORA_OK;
}

lora_err_t lora_start_receive_task(uint32_t interval_ms) {
    if (!s_initialized) {
        return LORA_ERR_NOT_INIT;
    }
    
    if (s_periodic_running) {
        LOGW(TAG, "Receive task already running");
        return LORA_OK;
    }
    
    if (interval_ms == 0) {
        interval_ms = 100;
    }
    
    LOGI(TAG, "Starting receive task, interval=%lu ms", interval_ms);
    
    /* 初始化周期任务 */
    tasker_task_init_mi(
        &s_periodic_task,
        (int)interval_ms,
        -1,  /* 无限运行 */
        "lora_rx",
        lora_periodic_task_fn,
        NULL
    );
    
    /* 注册到tasker */
    int ret = tasker_enqueue(&s_periodic_task);
    if (ret != 0) {
        LOGE(TAG, "Failed to enqueue receive task: %d", ret);
        return LORA_ERR_UART;
    }
    
    s_periodic_running = true;
    
    LOGI(TAG, "Receive task started");
    
    return LORA_OK;
}

lora_err_t lora_stop_receive_task(void) {
    if (!s_periodic_running) {
        return LORA_OK;
    }
    
    LOGI(TAG, "Stopping receive task...");
    
    /* 取消任务 */
    task_cancel(&s_periodic_task);
    
    s_periodic_running = false;
    
    LOGI(TAG, "Receive task stopped");
    
    return LORA_OK;
}

bool lora_is_initialized(void) {
    return s_initialized;
}
