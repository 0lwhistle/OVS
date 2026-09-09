/**
 * @file event_bus_internal.h
 * @brief 事件总线内部数据结构
 * 
 * 定义事件总线内部使用的数据结构，包括订阅者、上下文等。
 * 此文件仅在 event_bus.c 中使用，不对外暴露。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef EVENT_BUS_INTERNAL_H
#define EVENT_BUS_INTERNAL_H

#include "event_bus_types.h"
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              内部数据结构                                   */
/* ========================================================================== */

/**
 * @brief 事件处理函数类型
 * 
 * @param event 事件指针 (包含事件头和数据)
 * @param user_data 用户数据指针 (订阅时传入)
 * @return 0 成功，非0 表示处理失败
 */
typedef int (*event_handler_fn_t)(const event_t* event, void* user_data);

/**
 * @brief 订阅者结构
 * 
 * 表示一个事件订阅者，包含处理函数和用户数据
 */
typedef struct {
    event_type_t event_type;        /**< 订阅的事件类型 */
    event_handler_fn_t handler;     /**< 事件处理函数 */
    void* user_data;                /**< 用户数据指针 */
    uint32_t id;                    /**< 订阅者唯一ID */
    bool active;                    /**< 是否激活 */
} subscriber_t;

/**
 * @brief 订阅句柄
 * 
 * 用于取消订阅的句柄，由 event_bus_subscribe 返回
 */
typedef struct {
    uint32_t subscriber_id;         /**< 订阅者ID */
    event_type_t event_type;        /**< 事件类型 */
} event_subscription_t;

/**
 * @brief 队列节点结构
 * 
 * 事件队列中的节点，包含事件指针和总大小
 */
typedef struct {
    event_t* event;                 /**< 事件指针 */
    size_t total_size;              /**< 事件总大小 (event_t + data_len) */
} queue_node_t;

/**
 * @brief 事件总线上下文
 * 
 * 事件总线的核心数据结构，包含所有状态信息
 * 
 * 注意: 此结构体使用 void* 指针来避免对 FreeRTOS 的直接依赖
 *       实际类型在 event_bus.c 中定义
 */
typedef struct {
    void* queue;                    /**< 事件队列句柄 (QueueHandle_t) */
    void* task_handle;              /**< 事件处理任务句柄 (port层) */              /**< 事件处理任务句柄 (TaskHandle_t) */
    
    /** 订阅者数组 */
    subscriber_t subscribers[EVENT_BUS_MAX_SUBSCRIBERS];
    
    uint32_t next_subscriber_id;    /**< 下一个订阅者ID */
    int subscriber_count;           /**< 当前订阅者数量 */
    
    void* lock;                     /**< 互斥锁 (port层句柄) */
    bool initialized;               /**< 是否已初始化 */
    
    /** 统计信息（C11 原子计数，多任务无锁安全，5.1 修复项3） */
    struct {
        atomic_uint events_published;   /**< 已发布事件数 */
        atomic_uint events_processed;   /**< 已处理事件数 */
        atomic_uint events_dropped;     /**< 丢弃的事件数 (队列满) */
        atomic_uint handler_errors;     /**< 处理函数错误数 */
    } stats;
} event_bus_context_t;

/* ========================================================================== */
/*                              内部函数声明                                   */
/* ========================================================================== */

/**
 * @brief 获取事件总线上下文
 * 
 * @return 事件总线上下文指针
 */
event_bus_context_t* event_bus_get_context(void);

/**
 * @brief 事件处理任务函数
 * 
 * 在独立任务中运行，从队列中取出事件并分发给订阅者
 * 
 * @param arg 任务参数 (未使用)
 */
void event_bus_process_task(void* arg);

/**
 * @brief 分发事件给订阅者
 * 
 * 遍历订阅者数组，将事件分发给匹配的订阅者
 * 
 * @param event 事件指针
 */
void event_bus_dispatch_event(const event_t* event);

/**
 * @brief 计算事件总大小
 * 
 * @param data_len 事件数据长度
 * @return 事件总大小 (event_t + data_len)
 */
static inline size_t event_bus_calc_total_size(uint16_t data_len) {
    return sizeof(event_t) + data_len;
}

/**
 * @brief 创建事件对象
 * 
 * 分配内存并初始化事件头
 * 
 * @param type 事件类型
 * @param data 事件数据指针 (可为NULL)
 * @param data_len 数据长度
 * @return 事件指针，失败返回NULL
 */
event_t* event_bus_create_event(event_type_t type, const void* data, uint16_t data_len);

/**
 * @brief 销毁事件对象
 * 
 * 释放事件占用的内存
 * 
 * @param event 事件指针
 */
void event_bus_destroy_event(event_t* event);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_BUS_INTERNAL_H */
