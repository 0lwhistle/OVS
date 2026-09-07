/**
 * @file main.c
 * @brief 应用程序入口（使用holder管理硬件模块）
 */
#include "tasker.h"        // 公共 tasker API
#include "event_bus.h"     // 公共事件总线 API
#include "web.h"           // 公共 web API
#include "wifi.h"          // 公共 wifi API
#include "ota.h"           // 公共 OTA API
#include "heartbeat.h"     // 公共 heartbeat API
#include "logger.h"        // 公共日志 API
#include "holder.h"        // 全局模块注册表
#include "tasker_test.h"   // tasker 测试

/* 外设模块头文件 */
#include "st7789.h"        // ST7789 显示屏
#include "w25q128.h"       // W25Q128 Flash 存储
#include "lora.h"          // LoRa 无线模块
#include "audio_module.h"  // 音频模块
#include "aht30.h"         // AHT30 温湿度传感器
#include "cst816s.h"       // CST816S 触摸屏

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "nvs_flash.h"

static const char* TAG = "[MAIN]";

/**
 * @brief 模块初始化包装函数
 * 
 * 适配holder需要的int返回类型初始化函数
 */

// ST7789显示屏初始化包装
static int st7789_init_wrapper(void) {
    st7789_err_t ret = st7789_init();
    return (ret == ST7789_OK) ? 0 : -1;
}

// W25Q128 Flash初始化包装
static int w25q128_init_wrapper(void) {
    w25q128_err_t ret = w25q128_init();
    return (ret == W25Q128_OK) ? 0 : -1;
}

// LoRa模块初始化包装
static int lora_init_wrapper(void) {
    lora_err_t ret = lora_init();
    return (ret == LORA_OK) ? 0 : -1;
}

// 音频模块初始化包装
static int audio_module_init_wrapper(void) {
    audio_module_handle_t handle = NULL;
    audio_module_err_t ret = audio_module_init(&handle);
    return (ret == AUDIO_MODULE_OK) ? 0 : -1;
}

// AHT30传感器初始化包装
static int aht30_init_wrapper(void) {
    aht30_err_t ret = aht30_init();
    return (ret == AHT30_OK) ? 0 : -1;
}

// CST816S触摸屏初始化包装
static int cst816s_init_wrapper(void) {
    cst816s_err_t ret = cst816s_init();
    return (ret == CST816S_OK) ? 0 : -1;
}

/**
 * @brief Web 服务器任务函数
 */
static void web_server_task(void *arg) {
    (void)arg;
    
    // 初始化 SPIFFS 并解压 web 资源
    if (web_spiffs_init() != 0) {
        LOGE(TAG, "SPIFFS init failed, web server will not start");
        vTaskDelete(NULL);
        return;
    }

    // 启动 Web 服务器 (内部轮询循环，不会返回)
    web_server_start();

    vTaskDelete(NULL);
}

/**
 * @brief 应用程序入口函数
 */
void app_main(void) {
    LOGI(TAG, "========================================");
    LOGI(TAG, "  ESP32-S3 SYSTEM START");
    LOGI(TAG, "========================================");

    // 初始化 NVS (Wi-Fi 驱动依赖 NVS 存储配置)
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);
    LOGI(TAG, "NVS initialized");

    // 初始化事件总线 (必须在其他模块前初始化)
    event_bus_err_t eb_ret = event_bus_init();
    if (eb_ret != EVENT_BUS_OK) {
        LOGE(TAG, "Event bus init failed: %d", eb_ret);
        return;
    }
    LOGI(TAG, "Event bus initialized");

    // 初始化 Wi-Fi (STA 模式)
    wifi_init();

    // 初始化 OTA 模块 (创建队列和后台写入任务)
    ota_init();

    // 初始化 tasker 系统 (必须先初始化，后续任务才能注册)
    int ret = tasker_init();
    if (ret != 0) {
        LOGE(TAG, "tasker_init failed: %d", ret);
        return;
    }

    // 初始化心跳任务 (每秒缓存 WiFi 信号强度和运行时间)
    heartbeat_init();
    LOGI(TAG, "Heartbeat initialized");

    // ==================== 使用holder管理硬件模块 ====================
    LOGI(TAG, "Initializing hardware modules with holder...");

    // 初始化holder
    holder_err_t holder_ret = holder_init();
    if (holder_ret != HOLDER_OK) {
        LOGE(TAG, "Holder init failed: %d", holder_ret);
        return;
    }

    // 注册所有硬件模块到holder
    holder_register_module("st7789", st7789_init_wrapper, true, NULL);      // 显示屏，必需
    holder_register_module("w25q128", w25q128_init_wrapper, true, NULL);    // Flash存储，必需
    holder_register_module("lora", lora_init_wrapper, false, NULL);         // LoRa模块，非必需
    holder_register_module("audio", audio_module_init_wrapper, false, NULL);// 音频模块，非必需
    holder_register_module("aht30", aht30_init_wrapper, false, NULL);       // 温湿度传感器，非必需
    holder_register_module("cst816s", cst816s_init_wrapper, false, NULL);   // 触摸屏，非必需

    // 初始化所有注册的硬件模块
    holder_ret = holder_init_all(true);  // 遇到必需模块错误时停止
    if (holder_ret != HOLDER_OK) {
        LOGE(TAG, "Some required hardware modules failed to initialize");
        // 继续运行，但记录错误
    }

    // 打印模块状态报告
    holder_print_status();

    // ==================== 启动周期性任务 ====================
    LOGI(TAG, "Starting periodic tasks...");

    // 只有模块初始化成功后才启动周期性任务
    if (holder_is_module_ready("aht30")) {
        aht30_start_periodic_read(2000);
        LOGI(TAG, "AHT30 periodic read started (2000ms)");
    } else {
        LOGW(TAG, "AHT30 not ready, skipping periodic read");
    }

    if (holder_is_module_ready("cst816s")) {
        cst816s_start_periodic_read(50);
        LOGI(TAG, "CST816S periodic read started (50ms)");
    } else {
        LOGW(TAG, "CST816S not ready, skipping periodic read");
    }

    if (holder_is_module_ready("lora")) {
        lora_start_receive_task(100);
        LOGI(TAG, "LoRa receive task started (100ms)");
    } else {
        LOGW(TAG, "LoRa not ready, skipping receive task");
    }

    // 启动 Web 服务器任务 (SPIFFS 初始化 + Mongoose)
    // 注意: web_server_task 内部会轮询，不会返回，所以用独立任务运行
    TaskHandle_t web_task_handle = NULL;
    xTaskCreatePinnedToCore(
        web_server_task,
        "web_server",
        8192,
        NULL,
        5,
        &web_task_handle,
        1  // 在 CPU1 上运行
    );
    if (web_task_handle == NULL) {
        LOGE(TAG, "Failed to create web server task");
    } else {
        LOGI(TAG, "Web server task created on CPU1");
    }

    LOGI(TAG, "========================================");
    LOGI(TAG, "  SYSTEM INITIALIZATION COMPLETE");
    LOGI(TAG, "========================================");

    // 运行 tasker 测试
    // tasker_test_all();
}
