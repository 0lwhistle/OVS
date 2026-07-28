#include "tasker.h"
#include "web.h"
#include "wifi.h"
#include "ota.h"
#include "heartbeat.h"
#include "../modules/logger/logger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "nvs_flash.h"
#include "tasker_test.h"


static const char* TAG = "[MAIN]";

// // Web 服务器任务函数
// static void web_server_task(void *arg) {
//     // 1. 初始化 SPIFFS 并解压 web 资源
//     if (web_spiffs_init() != 0) {
//         LOGE(TAG, "SPIFFS init failed, web server will not start");
//         vTaskDelete(NULL);
//         return;
//     }

//     // 2. 启动 Web 服务器（内部轮询循环，不会返回）
//     web_server_start();

//     vTaskDelete(NULL);
// }

// void app_main(void)
// {
//     LOGI(TAG, "========================================");
//     LOGI(TAG, "  ESP32-S3 SYSTEM START");
//     LOGI(TAG, "========================================");

//     // 初始化 NVS（Wi-Fi 驱动依赖 NVS 存储配置）
//     esp_err_t nvs_ret = nvs_flash_init();
//     if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
//         ESP_ERROR_CHECK(nvs_flash_erase());
//         nvs_ret = nvs_flash_init();
//     }
//     ESP_ERROR_CHECK(nvs_ret);
//     LOGI(TAG, "NVS initialized");

//     // 初始化 Wi-Fi（STA 模式）
//     wifi_init();

//     // 初始化 OTA 模块（创建队列和后台写入任务）
//     ota_init();

//     // 初始化 tasker 系统（必须先初始化，后续任务才能注册）
//     int ret = tasker_init();
//     if (ret != 0) {
//         LOGE(TAG, "tasker_init failed: %d", ret);
//         return;
//     }

//     // 初始化心跳任务（每秒缓存 WiFi 信号强度和运行时间）
//     heartbeat_init();
//     LOGI(TAG, "Heartbeat initialized");

//     // 启动 Web 服务器任务（SPIFFS 初始化 + Mongoose）
//     // 注意：web_server_task 内部会轮询，不会返回，所以用独立任务运行
//     TaskHandle_t web_task_handle = NULL;
//     xTaskCreatePinnedToCore(
//         web_server_task,
//         "web_server",
//         8192,
//         NULL,
//         5,
//         &web_task_handle,
//         1  // 在 CPU1 上运行
//     );
//     if (web_task_handle == NULL) {
//         LOGE(TAG, "Failed to create web server task");
//     } else {
//         LOGI(TAG, "Web server task created on CPU1");
//     }

// }

void app_main(){
	tasker_init();
	tasker_test_all();
}
