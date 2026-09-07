#include "logger.h"
/**
 * @file event_bus_test.c
 * @brief 事件总线测试
 * 
 * 测试事件总线的各项功能
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "event_bus.h"
#include "logger.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char* TAG = "[EVENT_BUS_TEST]";

/* ========================================================================== */
/*                              测试用例                                       */
/* ========================================================================== */

/** 测试统计 */
static struct {
    int total;
    int passed;
    int failed;
} s_test_stats = {0, 0, 0};

/** 测试宏 */
#define TEST_ASSERT(condition, message) do { \
    s_test_stats.total++; \
    if (condition) { \
        s_test_stats.passed++; \
        LOGI(TAG, "  PASS: %s", message); \
    } else { \
        s_test_stats.failed++; \
        LOGE(TAG, "  FAIL: %s", message); \
    } \
} while(0)

/* ========================================================================== */
/*                              测试回调                                       */
/* ========================================================================== */

/** 测试用事件计数器 */
static int s_test_event_count = 0;
static event_type_t s_last_event_type = 0;

/**
 * @brief 测试事件处理函数
 */
static int test_event_handler(const event_t* event, void* user_data) {
    (void)user_data;
    
    s_test_event_count++;
    s_last_event_type = event->header.type;
    
    LOGD(TAG, "Handler called: event=0x%08X, count=%d",
         event->header.type, s_test_event_count);
    
    return 0;
}

/**
 * @brief 测试事件处理函数 (返回错误)
 */
static int test_event_handler_error(const event_t* event, void* user_data) {
    (void)event;
    (void)user_data;
    return -1;  // 返回错误
}

/* ========================================================================== */
/*                              测试用例实现                                   */
/* ========================================================================== */

/**
 * @brief 测试初始化和反初始化
 */
static void test_init_deinit(void) {
    LOGI(TAG, "=== Test: init/deinit ===");
    
    /* 测试初始化 */
    event_bus_err_t ret = event_bus_init();
    TEST_ASSERT(ret == EVENT_BUS_OK, "Init should succeed");
    TEST_ASSERT(event_bus_is_initialized(), "Should be initialized");
    
    /* 测试重复初始化 */
    ret = event_bus_init();
    TEST_ASSERT(ret == EVENT_BUS_ERR_ALREADY_INIT, "Double init should fail");
    
    /* 测试反初始化 */
    ret = event_bus_deinit();
    TEST_ASSERT(ret == EVENT_BUS_OK, "Deinit should succeed");
    TEST_ASSERT(!event_bus_is_initialized(), "Should not be initialized");
    
    /* 测试重复反初始化 */
    ret = event_bus_deinit();
    TEST_ASSERT(ret == EVENT_BUS_ERR_NOT_INIT, "Double deinit should fail");
    
    /* 重新初始化，用于后续测试 */
    ret = event_bus_init();
    TEST_ASSERT(ret == EVENT_BUS_OK, "Re-init should succeed");
}

/**
 * @brief 测试事件发布
 */
static void test_publish(void) {
    LOGI(TAG, "=== Test: publish ===");
    
    /* 测试发布无数据事件 */
    event_bus_err_t ret = EVENT_BUS_PUBLISH_EMPTY(EVENT_SYSTEM_STARTUP);
    TEST_ASSERT(ret == EVENT_BUS_OK, "Publish empty event should succeed");
    
    /* 测试发布带数据事件 */
    event_sensor_temp_humidity_t temp_data = {
        .temperature = 25.5f,
        .humidity = 60.0f,
        .timestamp = 123456
    };
    ret = EVENT_BUS_PUBLISH(EVENT_SENSOR_TEMP_HUMIDITY, &temp_data);
    TEST_ASSERT(ret == EVENT_BUS_OK, "Publish event with data should succeed");
    
    /* 测试发布过大事件 */
    uint8_t big_data[EVENT_BUS_MAX_EVENT_SIZE + 1];
    ret = event_bus_publish(EVENT_SYSTEM_ERROR, big_data, sizeof(big_data));
    TEST_ASSERT(ret == EVENT_BUS_ERR_INVALID_PARAM, "Publish oversized event should fail");
}

/**
 * @brief 测试事件订阅
 */
static void test_subscribe(void) {
    LOGI(TAG, "=== Test: subscribe ===");
    
    /* 测试订阅 */
    event_subscription_t* sub = event_bus_subscribe(
        EVENT_WIFI_CONNECTED,
        test_event_handler,
        NULL
    );
    TEST_ASSERT(sub != NULL, "Subscribe should succeed");
    
    /* 测试订阅多个事件 */
    event_subscription_t* sub2 = event_bus_subscribe(
        EVENT_SENSOR_TEMP_HUMIDITY,
        test_event_handler,
        NULL
    );
    TEST_ASSERT(sub2 != NULL, "Subscribe second event should succeed");
    
    /* 测试取消订阅 */
    event_bus_err_t ret = event_bus_unsubscribe(sub);
    TEST_ASSERT(ret == EVENT_BUS_OK, "Unsubscribe should succeed");
    
    ret = event_bus_unsubscribe(sub2);
    TEST_ASSERT(ret == EVENT_BUS_OK, "Unsubscribe second should succeed");
    
    /* 测试重复取消订阅 (sub已释放，不应崩溃) */
    // 注意: 此测试可能导致未定义行为，实际使用中应避免
}

/**
 * @brief 测试事件处理
 */
static void test_event_handling(void) {
    LOGI(TAG, "=== Test: event handling ===");
    
    /* 重置计数器 */
    s_test_event_count = 0;
    s_last_event_type = 0;
    
    /* 订阅事件 */
    event_subscription_t* sub = event_bus_subscribe(
        EVENT_WIFI_CONNECTED,
        test_event_handler,
        NULL
    );
    TEST_ASSERT(sub != NULL, "Subscribe should succeed");
    
    /* 发布事件 */
    event_wifi_connected_t wifi_data = {
        .rssi = -50,
        .authmode = 0,
        .ip_addr = 0
    };
    strncpy(wifi_data.ssid, "TestAP", sizeof(wifi_data.ssid) - 1);
    
    event_bus_err_t ret = EVENT_BUS_PUBLISH(EVENT_WIFI_CONNECTED, &wifi_data);
    TEST_ASSERT(ret == EVENT_BUS_OK, "Publish should succeed");
    
    /* 等待事件处理 */
    vTaskDelay(pdMS_TO_TICKS(100));
    
    /* 验证事件是否被处理 */
    TEST_ASSERT(s_test_event_count == 1, "Event should be handled once");
    TEST_ASSERT(s_last_event_type == EVENT_WIFI_CONNECTED, "Event type should match");
    
    /* 清理 */
    event_bus_unsubscribe(sub);
}

/**
 * @brief 测试多个订阅者
 */
static void test_multiple_subscribers(void) {
    LOGI(TAG, "=== Test: multiple subscribers ===");
    
    /* 重置计数器 */
    s_test_event_count = 0;
    
    /* 订阅同一事件的多个处理函数 */
    event_subscription_t* sub1 = event_bus_subscribe(
        EVENT_SYSTEM_STARTUP,
        test_event_handler,
        NULL
    );
    
    event_subscription_t* sub2 = event_bus_subscribe(
        EVENT_SYSTEM_STARTUP,
        test_event_handler,
        NULL
    );
    
    TEST_ASSERT(sub1 != NULL, "Subscribe 1 should succeed");
    TEST_ASSERT(sub2 != NULL, "Subscribe 2 should succeed");
    
    /* 发布事件 */
    EVENT_BUS_PUBLISH_EMPTY(EVENT_SYSTEM_STARTUP);
    
    /* 等待事件处理 */
    vTaskDelay(pdMS_TO_TICKS(100));
    
    /* 验证两个处理函数都被调用 */
    TEST_ASSERT(s_test_event_count == 2, "Both handlers should be called");
    
    /* 清理 */
    event_bus_unsubscribe(sub1);
    event_bus_unsubscribe(sub2);
}

/**
 * @brief 测试统计信息
 */
static void test_stats(void) {
    LOGI(TAG, "=== Test: stats ===");
    
    /* 获取统计信息 */
    uint32_t published = 0, processed = 0, dropped = 0, errors = 0;
    event_bus_err_t ret = event_bus_get_stats(&published, &processed, &dropped, &errors);
    TEST_ASSERT(ret == EVENT_BUS_OK, "Get stats should succeed");
    TEST_ASSERT(published > 0, "Published count should be > 0");
    TEST_ASSERT(processed > 0, "Processed count should be > 0");
    
    /* 重置统计信息 */
    ret = event_bus_reset_stats();
    TEST_ASSERT(ret == EVENT_BUS_OK, "Reset stats should succeed");
    
    ret = event_bus_get_stats(&published, &processed, &dropped, &errors);
    TEST_ASSERT(published == 0, "Published count should be 0 after reset");
}

/**
 * @brief 测试状态查询
 */
static void test_status(void) {
    LOGI(TAG, "=== Test: status ===");
    
    int queue_size = -1;
    int subscriber_count = -1;
    
    event_bus_err_t ret = event_bus_get_status(&queue_size, &subscriber_count);
    TEST_ASSERT(ret == EVENT_BUS_OK, "Get status should succeed");
    TEST_ASSERT(queue_size >= 0, "Queue size should be >= 0");
    TEST_ASSERT(subscriber_count >= 0, "Subscriber count should be >= 0");
}

/**
 * @brief 测试调试函数
 */
static void test_debug_functions(void) {
    LOGI(TAG, "=== Test: debug functions ===");
    
    /* 测试获取事件类型名称 */
    const char* name = event_bus_get_type_name(EVENT_WIFI_CONNECTED);
    TEST_ASSERT(name != NULL, "Get type name should return non-NULL");
    TEST_ASSERT(strcmp(name, "WIFI_CONNECTED") == 0, "Type name should match");
    
    /* 测试获取错误码名称 */
    const char* err_name = event_bus_get_err_name(EVENT_BUS_OK);
    TEST_ASSERT(err_name != NULL, "Get err name should return non-NULL");
    TEST_ASSERT(strcmp(err_name, "OK") == 0, "Err name should match");
    
    /* 测试打印状态 (不会崩溃即通过) */
    event_bus_print_status();
    event_bus_print_subscribers();
}

/* ========================================================================== */
/*                              测试入口                                       */
/* ========================================================================== */

/**
 * @brief 运行所有事件总线测试
 */
void event_bus_test_all(void) {
    LOGI(TAG, "========================================");
    LOGI(TAG, "  Event Bus Test Suite");
    LOGI(TAG, "========================================");
    
    /* 重置统计 */
    s_test_stats.total = 0;
    s_test_stats.passed = 0;
    s_test_stats.failed = 0;
    
    /* 运行测试 */
    test_init_deinit();
    test_publish();
    test_subscribe();
    test_event_handling();
    test_multiple_subscribers();
    test_stats();
    test_status();
    test_debug_functions();
    
    /* 打印测试结果 */
    LOGI(TAG, "========================================");
    LOGI(TAG, "  Test Results: %d/%d passed",
         s_test_stats.passed, s_test_stats.total);
    LOGI(TAG, "========================================");
    
    if (s_test_stats.failed > 0) {
        LOGE(TAG, "  %d tests FAILED!", s_test_stats.failed);
    } else {
        LOGI(TAG, "  All tests PASSED!");
    }
}
