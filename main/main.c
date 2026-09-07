/**
 * @file main.c
 * @brief 应用程序入口
 */
#include "tasker.h"        // 公共 tasker API
#include "event_bus.h"     // 公共事件总线 API
#include "web.h"           // 公共 web API
#include "wifi.h"          // 公共 wifi API
#include "ota.h"           // 公共 OTA API
#include "heartbeat.h"     // 公共 heartbeat API
#include "logger.h"        // 公共日志 API
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

    // 初始化外设模块
    LOGI(TAG, "Initializing peripheral modules...");

    // 初始化 ST7789 显示屏
    st7789_init();
    LOGI(TAG, "ST7789 display initialized");

    // 初始化 W25Q128 Flash 存储
    w25q128_init();
    LOGI(TAG, "W25Q128 flash initialized");

    // 初始化 LoRa 无线模块
    lora_init();
    LOGI(TAG, "LoRa module initialized");

    // 初始化音频模块
    audio_module_handle_t audio_handle = NULL;
    audio_module_init(&audio_handle);
    LOGI(TAG, "Audio module initialized");

    // 初始化 AHT30 温湿度传感器
    aht30_init();
    LOGI(TAG, "AHT30 sensor initialized");

    // 初始化 CST816S 触摸屏
    cst816s_init();
    LOGI(TAG, "CST816S touch initialized");

    // 启动周期性任务
    LOGI(TAG, "Starting periodic tasks...");

    // 启动 AHT30 周期性读取 (每2秒)
    aht30_start_periodic_read(2000);
    LOGI(TAG, "AHT30 periodic read started (2000ms)");

    // 启动 CST816S 周期性读取 (每50ms)
    cst816s_start_periodic_read(50);
    LOGI(TAG, "CST816S periodic read started (50ms)");

    // 启动 LoRa 周期性接收 (每100ms)
    lora_start_receive_task(100);
    LOGI(TAG, "LoRa receive task started (100ms)");

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
