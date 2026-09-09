/**
 * @file event_bus.h
 * @brief 事件总线公共API
 * 
 * 事件总线提供模块间的发布-订阅通信机制，实现模块解耦。
 * 
 * 功能特性:
 * - 支持多种事件类型，按模块分组
 * - 线程安全的订阅/取消订阅操作
 * - 异步事件处理，不阻塞发布者
 * - 固定大小队列，内存占用可预测
 * - 事件数据支持柔性数组，灵活高效
 * 
 * 使用流程:
 * 1. 系统启动时调用 event_bus_init() 初始化
 * 2. 各模块通过 event_bus_subscribe() 订阅感兴趣的事件
 * 3. 事件发生时通过 event_bus_publish() 发布事件
 * 4. 事件总线在独立任务中处理事件，调用订阅者的回调函数
 * 
 * @code
 * // 示例: 订阅WiFi连接事件
 * static int on_wifi_connected(const event_t* event, void* user_data) {
 *     const event_wifi_connected_t* data = (const event_wifi_connected_t*)event->data;
 *     printf("WiFi connected: %s\n", data->ssid);
 *     return 0;
 * }
 * 
 * // 订阅
 * event_subscription_t* sub = event_bus_subscribe(
 *     EVENT_WIFI_CONNECTED,
 *     on_wifi_connected,
 *     NULL
 * );
 * 
 * // 发布事件
 * event_wifi_connected_t data = { .rssi = -50 };
 * strncpy(data.ssid, "MyAP", sizeof(data.ssid) - 1);
 * EVENT_BUS_PUBLISH(EVENT_WIFI_CONNECTED, &data);
 * 
 * // 取消订阅
 * event_bus_unsubscribe(sub);
 * @endcode
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include "event_bus_types.h"
#include "event_bus_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              初始化与销毁                                   */
/* ========================================================================== */

/**
 * @brief 初始化事件总线
 * 
 * 创建事件队列和处理任务。必须在其他模块使用事件总线前调用。
 * 
 * 初始化内容:
 * - 创建事件队列 (EVENT_BUS_QUEUE_SIZE 个槽位)
 * - 创建事件处理任务 (EVENT_BUS_TASK_STACK_SIZE 栈大小)
 * - 初始化订阅者数组
 * - 创建互斥锁
 * 
 * @return EVENT_BUS_OK 成功
 * @return EVENT_BUS_ERR_ALREADY_INIT 已经初始化
 * @return EVENT_BUS_ERR_NO_MEMORY 内存分配失败
 * 
 * @note 应在 app_main() 中尽早调用
 * 
 * @code
 * void app_main(void) {
 *     // 初始化事件总线 (必须在其他模块前)
 *     if (event_bus_init() != EVENT_BUS_OK) {
 *         ESP_LOGE(TAG, "Event bus init failed");
 *         return;
 *     }
 *     
 *     // 初始化其他模块...
 *     wifi_init();
 *     sensor_init();
 * }
 * @endcode
 */
event_bus_err_t event_bus_init(void);

/**
 * @brief 反初始化事件总线
 * 
 * 停止事件处理任务，清空队列，释放资源。
 * 
 * @return EVENT_BUS_OK 成功
 * @return EVENT_BUS_ERR_NOT_INIT 未初始化
 * 
 * @warning 调用此函数后，所有订阅都将失效
 */
event_bus_err_t event_bus_deinit(void);

/* ========================================================================== */
/*                              事件发布                                       */
/* ========================================================================== */

/**
 * @brief 发布事件
 * 
 * 将事件放入队列，立即返回。事件数据会被拷贝到内部缓冲区。
 * 
 * @param type 事件类型 (event_type_t)
 * @param data 事件数据指针 (可为NULL，表示无数据)
 * @param data_len 数据长度 (字节)
 * 
 * @return EVENT_BUS_OK 成功
 * @return EVENT_BUS_ERR_NOT_INIT 未初始化
 * @return EVENT_BUS_ERR_INVALID_PARAM 参数无效
 * @return EVENT_BUS_ERR_QUEUE_FULL 队列已满
 * @return EVENT_BUS_ERR_NO_MEMORY 内存分配失败
 * 
 * @note 此函数是线程安全的，可在任何任务中调用
 * @note 事件数据会被拷贝，调用者可在函数返回后释放数据
 * 
 * @code
 * // 发布带数据的事件
 * event_sensor_temp_humidity_t data = {
 *     .temperature = 25.5f,
 *     .humidity = 60.0f,
 *     .timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS
 * };
 * event_bus_err_t ret = event_bus_publish(
 *     EVENT_SENSOR_TEMP_HUMIDITY,
 *     &data,
 *     sizeof(data)
 * );
 * 
 * // 发布无数据事件
 * event_bus_publish(EVENT_SYSTEM_STARTUP, NULL, 0);
 * @endcode
 */
event_bus_err_t event_bus_publish(event_type_t type, const void* data, size_t data_len);

/* ========================================================================== */
/*                              事件订阅                                       */
/* ========================================================================== */

/**
 * @brief 订阅事件
 * 
 * 注册一个事件处理函数，当指定类型的事件发生时会被调用。
 * 
 * @param event_type 要订阅的事件类型
 * @param handler 事件处理函数指针
 * @param user_data 用户数据指针，会传递给处理函数 (可为NULL)
 * 
 * @return 订阅句柄指针，用于取消订阅
 * @return NULL 失败 (参数无效或订阅者数量已达上限)
 * 
 * @note 此函数是线程安全的
 * @note 同一个处理函数可以订阅多种事件类型
 * @note 同一种事件类型可以有多个订阅者
 * 
 * @code
 * // 定义处理函数
 * static int my_handler(const event_t* event, void* user_data) {
 *     printf("Event received: type=0x%08X\n", event->header.type);
 *     return 0;
 * }
 * 
 * // 订阅
 * event_subscription_t* sub = event_bus_subscribe(
 *     EVENT_WIFI_CONNECTED,
 *     my_handler,
 *     NULL
 * );
 * if (sub == NULL) {
 *     printf("Subscribe failed\n");
 * }
 * @endcode
 */
event_subscription_t* event_bus_subscribe(event_type_t event_type, 
                                          event_handler_fn_t handler, 
                                          void* user_data);

/**
 * @brief 取消订阅
 * 
 * 取消之前注册的事件订阅。
 * 
 * @param subscription 订阅句柄指针 (由 event_bus_subscribe 返回)
 * 
 * @return EVENT_BUS_OK 成功
 * @return EVENT_BUS_ERR_INVALID_PARAM 参数无效
 * @return EVENT_BUS_ERR_NOT_FOUND 订阅者未找到
 * 
 * @note 此函数是线程安全的
 * @note 取消订阅后，句柄指针失效，不应再使用
 * 
 * @code
 * event_subscription_t* sub = event_bus_subscribe(...);
 * 
 * // ... 使用中 ...
 * 
 * // 取消订阅
 * event_bus_err_t ret = event_bus_unsubscribe(sub);
 * sub = NULL;  // 置空，避免误用
 * @endcode
 */
event_bus_err_t event_bus_unsubscribe(event_subscription_t* subscription);

/* ========================================================================== */
/*                              状态查询                                       */
/* ========================================================================== */

/**
 * @brief 获取事件总线状态
 * 
 * @param queue_size 当前队列中的事件数量 (可为NULL)
 * @param subscriber_count 当前订阅者数量 (可为NULL)
 * 
 * @return EVENT_BUS_OK 成功
 * @return EVENT_BUS_ERR_NOT_INIT 未初始化
 */
event_bus_err_t event_bus_get_status(int* queue_size, int* subscriber_count);

/**
 * @brief 获取事件总线统计信息
 * 
 * @param events_published 已发布事件数 (可为NULL)
 * @param events_processed 已处理事件数 (可为NULL)
 * @param events_dropped 丢弃的事件数 (可为NULL)
 * @param handler_errors 处理函数错误数 (可为NULL)
 * 
 * @return EVENT_BUS_OK 成功
 * @return EVENT_BUS_ERR_NOT_INIT 未初始化
 */
event_bus_err_t event_bus_get_stats(uint32_t* events_published, 
                                    uint32_t* events_processed,
                                    uint32_t* events_dropped,
                                    uint32_t* handler_errors);

/**
 * @brief 重置统计信息
 * 
 * @return EVENT_BUS_OK 成功
 * @return EVENT_BUS_ERR_NOT_INIT 未初始化
 */
event_bus_err_t event_bus_reset_stats(void);

/**
 * @brief 检查事件总线是否已初始化
 * 
 * @return true 已初始化
 * @return false 未初始化
 */
bool event_bus_is_initialized(void);

/**
 * @brief 获取事件池满退化堆分配的累计次数（诊断用）
 */
uint32_t event_bus_get_heap_fallback(void);

/* ========================================================================== */
/*                              调试辅助                                       */
/* ========================================================================== */

/**
 * @brief 获取事件类型名称
 * 
 * 用于调试日志输出
 * 
 * @param type 事件类型
 * @return 事件类型名称字符串
 */
const char* event_bus_get_type_name(event_type_t type);

/**
 * @brief 获取错误码名称
 * 
 * @param err 错误码
 * @return 错误码名称字符串
 */
const char* event_bus_get_err_name(event_bus_err_t err);

/**
 * @brief 打印事件总线状态
 * 
 * 输出当前队列状态、订阅者数量、统计信息等
 */
void event_bus_print_status(void);

/**
 * @brief 打印所有订阅者信息
 * 
 * 输出当前所有活跃的订阅者列表
 */
void event_bus_print_subscribers(void);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_BUS_H */
