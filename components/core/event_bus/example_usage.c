#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
/**
 * @file example_usage.c
 * @brief 事件总线使用示例
 * 
 * 展示如何在实际项目中使用事件总线
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "event_bus.h"
#include "logger.h"

#include <stdio.h>

static const char* TAG = "[EXAMPLE]";

/* ========================================================================== */
/*                              示例: WiFi模块                                 */
/* ========================================================================== */

/**
 * @brief WiFi模块: 发布WiFi连接事件
 */
void example_wifi_on_connected(const char* ssid, int rssi) {
    LOGI(TAG, "WiFi connected to %s, RSSI: %d", ssid, rssi);
    
    /* 准备事件数据 */
    event_wifi_connected_t data = {
        .rssi = rssi,
        .authmode = 0,
        .ip_addr = 0
    };
    strncpy(data.ssid, ssid, sizeof(data.ssid) - 1);
    
    /* 发布事件 */
    event_bus_err_t ret = EVENT_BUS_PUBLISH(EVENT_WIFI_CONNECTED, &data);
    if (ret != EVENT_BUS_OK) {
        LOGE(TAG, "Failed to publish WiFi connected event: %s",
             event_bus_get_err_name(ret));
    }
}

/* ========================================================================== */
/*                              示例: 传感器模块                               */
/* ========================================================================== */

/**
 * @brief 传感器模块: 发布温湿度事件
 */
void example_sensor_update(float temp, float humidity) {
    LOGI(TAG, "Sensor update: temp=%.1f, humidity=%.1f", temp, humidity);
    
    /* 准备事件数据 */
    event_sensor_temp_humidity_t data = {
        .temperature = temp,
        .humidity = humidity,
        .timestamp = 0  /* 实际应用中应使用真实时间戳 */
    };
    
    /* 发布事件 */
    EVENT_BUS_PUBLISH(EVENT_SENSOR_TEMP_HUMIDITY, &data);
}

/* ========================================================================== */
/*                              示例: Web模块 (订阅者)                         */
/* ========================================================================== */

/**
 * @brief Web模块: 处理WiFi连接事件
 */
static int web_on_wifi_connected(const event_t* event, void* user_data) {
    (void)user_data;
    
    const event_wifi_connected_t* data = 
        (const event_wifi_connected_t*)event->data;
    
    LOGI(TAG, "[Web] WiFi connected: %s, RSSI: %d", data->ssid, data->rssi);
    
    /* 在这里可以更新WebSocket推送、更新UI等 */
    
    return 0;
}

/**
 * @brief Web模块: 处理温湿度事件
 */
static int web_on_temp_humidity(const event_t* event, void* user_data) {
    (void)user_data;
    
    const event_sensor_temp_humidity_t* data = 
        (const event_sensor_temp_humidity_t*)event->data;
    
    LOGI(TAG, "[Web] Temp: %.1f°C, Humidity: %.1f%%", 
         data->temperature, data->humidity);
    
    /* 在这里可以更新WebSocket推送 */
    
    return 0;
}

/** Web模块的订阅句柄 */
static event_subscription_t* s_web_wifi_sub = NULL;
static event_subscription_t* s_web_sensor_sub = NULL;

/**
 * @brief Web模块初始化
 */
void example_web_module_init(void) {
    LOGI(TAG, "[Web] Initializing...");
    
    /* 订阅WiFi事件 */
    s_web_wifi_sub = event_bus_subscribe(
        EVENT_WIFI_CONNECTED,
        web_on_wifi_connected,
        NULL
    );
    
    if (s_web_wifi_sub == NULL) {
        LOGE(TAG, "[Web] Failed to subscribe to WiFi events");
    }
    
    /* 订阅传感器事件 */
    s_web_sensor_sub = event_bus_subscribe(
        EVENT_SENSOR_TEMP_HUMIDITY,
        web_on_temp_humidity,
        NULL
    );
    
    if (s_web_sensor_sub == NULL) {
        LOGE(TAG, "[Web] Failed to subscribe to sensor events");
    }
    
    LOGI(TAG, "[Web] Initialized, subscribed to events");
}

/**
 * @brief Web模块反初始化
 */
void example_web_module_deinit(void) {
    LOGI(TAG, "[Web] Deinitializing...");
    
    /* 取消订阅 */
    if (s_web_wifi_sub != NULL) {
        event_bus_unsubscribe(s_web_wifi_sub);
        s_web_wifi_sub = NULL;
    }
    
    if (s_web_sensor_sub != NULL) {
        event_bus_unsubscribe(s_web_sensor_sub);
        s_web_sensor_sub = NULL;
    }
    
    LOGI(TAG, "[Web] Deinitialized");
}

/* ========================================================================== */
/*                              示例: UI模块 (订阅者)                          */
/* ========================================================================== */

/**
 * @brief UI模块: 处理WiFi连接事件
 */
static int ui_on_wifi_connected(const event_t* event, void* user_data) {
    (void)user_data;
    
    const event_wifi_connected_t* data = 
        (const event_wifi_connected_t*)event->data;
    
    LOGI(TAG, "[UI] Updating WiFi status: %s", data->ssid);
    
    /* 在这里可以更新LVGL UI */
    
    return 0;
}

/** UI模块的订阅句柄 */
static event_subscription_t* s_ui_wifi_sub = NULL;

/**
 * @brief UI模块初始化
 */
void example_ui_module_init(void) {
    LOGI(TAG, "[UI] Initializing...");
    
    /* 订阅WiFi事件 */
    s_ui_wifi_sub = event_bus_subscribe(
        EVENT_WIFI_CONNECTED,
        ui_on_wifi_connected,
        NULL
    );
    
    if (s_ui_wifi_sub == NULL) {
        LOGE(TAG, "[UI] Failed to subscribe to WiFi events");
    }
    
    LOGI(TAG, "[UI] Initialized");
}

/**
 * @brief UI模块反初始化
 */
void example_ui_module_deinit(void) {
    LOGI(TAG, "[UI] Deinitializing...");
    
    if (s_ui_wifi_sub != NULL) {
        event_bus_unsubscribe(s_ui_wifi_sub);
        s_ui_wifi_sub = NULL;
    }
    
    LOGI(TAG, "[UI] Deinitialized");
}

/* ========================================================================== */
/*                              示例: 完整使用流程                             */
/* ========================================================================== */

/**
 * @brief 完整使用示例
 * 
 * 演示事件总线的完整使用流程
 */
void example_full_usage(void) {
    LOGI(TAG, "========================================");
    LOGI(TAG, "  Event Bus Full Usage Example");
    LOGI(TAG, "========================================");
    
    /* 1. 初始化事件总线 */
    LOGI(TAG, "Step 1: Initialize event bus");
    event_bus_err_t ret = event_bus_init();
    if (ret != EVENT_BUS_OK) {
        LOGE(TAG, "Failed to initialize event bus: %s",
             event_bus_get_err_name(ret));
        return;
    }
    
    /* 2. 初始化各模块 (订阅事件) */
    LOGI(TAG, "Step 2: Initialize modules");
    example_web_module_init();
    example_ui_module_init();
    
    /* 3. 模拟事件发生 */
    LOGI(TAG, "Step 3: Simulate events");
    
    /* 模拟WiFi连接 */
    example_wifi_on_connected("MyWiFi", -50);
    
    /* 模拟传感器更新 */
    example_sensor_update(25.5f, 60.0f);
    
    /* 等待事件处理 */
    LOGI(TAG, "Waiting for event processing...");
    vTaskDelay(pdMS_TO_TICKS(200));
    
    /* 4. 打印状态 */
    LOGI(TAG, "Step 4: Print status");
    event_bus_print_status();
    event_bus_print_subscribers();
    
    /* 5. 反初始化模块 */
    LOGI(TAG, "Step 5: Deinitialize modules");
    example_web_module_deinit();
    example_ui_module_deinit();
    
    /* 6. 反初始化事件总线 */
    LOGI(TAG, "Step 6: Deinitialize event bus");
    event_bus_deinit();
    
    LOGI(TAG, "========================================");
    LOGI(TAG, "  Example completed!");
    LOGI(TAG, "========================================");
}
