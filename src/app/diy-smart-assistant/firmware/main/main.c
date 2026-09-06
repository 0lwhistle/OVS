/**
 * ESP32-S3 桌面智能助手 - 主程序
 * 
 * 功能：
 * 1. LVGL图形界面
 * 2. LoRa远程通讯（对讲+留言）
 * 3. WiFi+蓝牙配网
 * 4. SD卡存储
 * 5. 音频采集播放
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "app_init.h"
#include "app_tasks.h"
#include "wifi_manager.h"
#include "ble_manager.h"
#include "lora_manager.h"
#include "audio_manager.h"
#include "display_manager.h"
#include "flash_manager.h"
#include "web_server.h"
#include "ui_manager.h"

static const char *TAG = "MAIN";

// 任务句柄
static TaskHandle_t xTaskMainHandle = NULL;
static TaskHandle_t xTaskDisplayHandle = NULL;
static TaskHandle_t xTaskLoRaHandle = NULL;
static TaskHandle_t xTaskAudioHandle = NULL;
static TaskHandle_t xTaskWebHandle = NULL;

// 消息队列
QueueHandle_t xQueueLoRaMsg = NULL;
QueueHandle_t xQueueAudioMsg = NULL;
QueueHandle_t xQueueUIMsg = NULL;

// 互斥锁
SemaphoreHandle_t xMutexSD = NULL;
SemaphoreHandle_t xMutexDisplay = NULL;

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 桌面智能助手启动 ===");
    
    // 1. 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 2. 创建消息队列
    xQueueLoRaMsg = xQueueCreate(10, sizeof(lora_msg_t));
    xQueueAudioMsg = xQueueCreate(5, sizeof(audio_msg_t));
    xQueueUIMsg = xQueueCreate(10, sizeof(ui_msg_t));
    
    // 3. 创建互斥锁
    xMutexSD = xSemaphoreCreateMutex();
    xMutexDisplay = xSemaphoreCreateMutex();
    
    // 4. 初始化硬件
    ESP_LOGI(TAG, "初始化硬件...");
    app_init_hardware();
    
    // 5. 初始化各个管理器
    ESP_LOGI(TAG, "初始化管理器...");
    flash_manager_init();
    display_manager_init();
    audio_manager_init();
    lora_manager_init();
    wifi_manager_init();
    ble_manager_init();
    web_server_init();
    ui_manager_init();
    
    // 6. 创建任务
    ESP_LOGI(TAG, "创建任务...");
    
    xTaskCreate(
        task_display,
        "task_display",
        4096,
        NULL,
        5,
        &xTaskDisplayHandle
    );
    
    xTaskCreate(
        task_lora,
        "task_lora",
        4096,
        NULL,
        4,
        &xTaskLoRaHandle
    );
    
    xTaskCreate(
        task_audio,
        "task_audio",
        4096,
        NULL,
        3,
        &xTaskAudioHandle
    );
    
    xTaskCreate(
        task_web_server,
        "task_web_server",
        4096,
        NULL,
        2,
        &xTaskWebHandle
    );
    
    xTaskCreate(
        task_main_loop,
        "task_main_loop",
        4096,
        NULL,
        1,
        &xTaskMainHandle
    );
    
    ESP_LOGI(TAG, "=== 系统启动完成 ===");
}

/**
 * 主循环任务
 * 处理系统状态、电源管理、看门狗等
 */
void task_main_loop(void *pvParameters)
{
    ESP_LOGI(TAG, "主循环任务启动");
    
    while (1) {
        // 1. 检查电池电压
        float battery_voltage = get_battery_voltage();
        ui_update_battery(battery_voltage);
        
        // 2. 检查系统状态
        check_system_status();
        
        // 3. 喂狗
        esp_task_wdt_reset();
        
        // 4. 延时
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
