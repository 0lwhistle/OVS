/**
 * @file event_bus.c
 * @brief 事件总线核心实现（REFACTORING_PLAN 5.1 四修复版）
 *
 * 架构设计:
 * - 事件队列: port 层有界队列（ESP=FreeRTOS 队列 / PC=pthread 环形队列）
 * - 订阅者表: 固定大小数组，互斥锁保护
 * - 处理任务: 独立任务从队列取出事件并分发给订阅者
 *
 * 5.1 修复（2026-09-09）:
 * 1. 锁外回调: dispatch 在锁内快照匹配的订阅者，锁外调用 handler——
 *    handler 内再订阅/退订/慢处理不再阻塞或死锁；
 * 2. 事件内存池: 定长块池（48 × 最大事件尺寸，建于 mem_pool），
 *    池满退化为堆分配兜底（reserved 标志区分，计数告警）；
 * 3. 统计原子化: C11 atomic 计数，多任务无锁安全；
 * 4. 名表全量: 由 event_bus_types.h 枚举自动生成，不再打印 UNKNOWN。
 *
 * API 完全不变，现有调用方零修改。
 */

#include "event_bus.h"
#include "event_bus_port.h"
#include "mem.h"
#include "mem_pool.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

/* ========================================================================== */
/*                              日志标签                                       */
/* ========================================================================== */

static const char* TAG = "[EVENT_BUS]";

/* ========================================================================== */
/*                              静态变量                                       */
/* ========================================================================== */

/** 事件总线上下文 (单例) */
static event_bus_context_t s_event_bus_ctx;

/** 订阅句柄池 (静态分配) */
static event_subscription_t s_subscription_pool[EVENT_BUS_MAX_SUBSCRIBERS];

/** 订阅句柄池使用标志 */
static bool s_subscription_pool_used[EVENT_BUS_MAX_SUBSCRIBERS];

/** 事件内存池（init 时创建，常驻） */
static mem_pool_t* s_event_pool = NULL;

/** 池满退化堆分配的计数（诊断用） */
static atomic_uint s_heap_fallback_count;

/** dispatch 快照上限：同一事件类型的并发匹配处理器上限，超出计数告警 */
#define EVENT_SNAPSHOT_MAX  16

/* ========================================================================== */
/*                              内部函数实现                                   */
/* ========================================================================== */

/**
 * @brief 获取事件总线上下文
 */
event_bus_context_t* event_bus_get_context(void) {
    return &s_event_bus_ctx;
}

/**
 * @brief 计算事件总大小
 */
static inline size_t calc_event_total_size(uint16_t data_len) {
    return sizeof(event_t) + data_len;
}

/**
 * @brief 创建事件对象
 *
 * 优先从事件池取块（零碎片），池满退化为堆分配（header.reserved=1 标记）
 */
event_t* event_bus_create_event(event_type_t type, const void* data, uint16_t data_len) {
    /* 参数检查 */
    if (data_len > 0 && data == NULL) {
        LOGE(TAG, "create_event: data is NULL but data_len > 0");
        return NULL;
    }

    if (data_len > EVENT_BUS_MAX_EVENT_SIZE) {
        LOGE(TAG, "create_event: data_len %u exceeds max %d", data_len, EVENT_BUS_MAX_EVENT_SIZE);
        return NULL;
    }

    size_t total_size = calc_event_total_size(data_len);
    bool from_pool = false;
    event_t* event = NULL;

    if (s_event_pool) {
        event = (event_t*)mem_pool_alloc(s_event_pool);
        from_pool = (event != NULL);
    }
    if (event == NULL) {
        event = (event_t*)mem_malloc(total_size);
        if (event == NULL) {
            LOGE(TAG, "create_event: alloc failed, size=%zu", total_size);
            return NULL;
        }
        atomic_fetch_add(&s_heap_fallback_count, 1);
    }

    /* 初始化事件头（reserved 兼作分配来源标志: 0=池, 1=堆） */
    event->header.type = type;
    event->header.data_len = data_len;
    event->header.reserved = from_pool ? 0 : 1;
    event->header.timestamp = bus_now_ms();

    /* 拷贝事件数据 */
    if (data != NULL && data_len > 0) {
        memcpy(event->data, data, data_len);
    }

    return event;
}

/**
 * @brief 销毁事件对象（按分配来源归还）
 */
void event_bus_destroy_event(event_t* event) {
    if (event == NULL) {
        return;
    }
    /* 带外归属判定: mem_pool_free 会把空闲链指针写进块首（覆盖事件头），
     * 块内标志不可靠，必须按地址范围判定归属 */
    if (s_event_pool && mem_pool_contains(s_event_pool, event)) {
        mem_pool_free(s_event_pool, event);
    } else {
        mem_free(event);
    }
}

/**
 * @brief 分发事件给订阅者（5.1 修复: 锁内快照、锁外回调）
 */
void event_bus_dispatch_event(const event_t* event) {
    if (event == NULL) {
        return;
    }

    event_bus_context_t* ctx = event_bus_get_context();
    struct {
        event_handler_fn_t handler;
        void* user_data;
    } snapshot[EVENT_SNAPSHOT_MAX];
    int handler_count = 0;
    int overflow = 0;

    /* 锁内: 仅快照匹配订阅者，绝不调用 handler */
    if (bus_lock_take(ctx->lock, 100)) {
        for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
            subscriber_t* sub = &ctx->subscribers[i];
            if (sub->active && sub->event_type == event->header.type) {
                if (handler_count < EVENT_SNAPSHOT_MAX) {
                    snapshot[handler_count].handler = sub->handler;
                    snapshot[handler_count].user_data = sub->user_data;
                    handler_count++;
                } else {
                    overflow++;
                }
            }
        }
        bus_lock_give(ctx->lock);
    } else {
        LOGE(TAG, "dispatch_event: failed to take lock");
        return;
    }

    if (overflow > 0) {
        LOGW(TAG, "dispatch_event: %d subscribers beyond snapshot cap %d (event 0x%08X)",
             overflow, EVENT_SNAPSHOT_MAX, event->header.type);
    }

    LOGD(TAG, "dispatch 0x%08X handlers=%d", event->header.type, handler_count);
    /* 锁外: 逐个调用 handler——此处再订阅/退订/慢处理均安全 */
    for (int i = 0; i < handler_count; i++) {
        int ret = snapshot[i].handler(event, snapshot[i].user_data);
        if (ret != 0) {
            atomic_fetch_add(&ctx->stats.handler_errors, 1);
            LOGW(TAG, "dispatch_event: handler returned %d for event 0x%08X",
                 ret, event->header.type);
        }
    }

    atomic_fetch_add(&ctx->stats.events_processed, 1);

    if (handler_count == 0) {
        LOGD(TAG, "dispatch_event: no handler for event 0x%08X", event->header.type);
    }
}

/**
 * @brief 事件处理任务
 */
void event_bus_process_task(void* arg) {
    (void)arg;

    event_bus_context_t* ctx = event_bus_get_context();
    queue_node_t node;

    LOGI(TAG, "Event bus task started");

    while (1) {
        if (bus_queue_recv(ctx->queue, &node, EVENT_BUS_POLL_INTERVAL_MS)) {
            event_bus_dispatch_event(node.event);
            event_bus_destroy_event(node.event);
        }
    }
}

/* ========================================================================== */
/*                              公共API实现                                    */
/* ========================================================================== */

/**
 * @brief 初始化事件总线
 */
event_bus_err_t event_bus_init(void) {
    event_bus_context_t* ctx = event_bus_get_context();

    /* 检查是否已初始化 */
    if (ctx->initialized) {
        LOGW(TAG, "Event bus already initialized");
        return EVENT_BUS_ERR_ALREADY_INIT;
    }

    LOGI(TAG, "Initializing event bus...");

    /* 初始化上下文 */
    memset(ctx, 0, sizeof(event_bus_context_t));

    /* 事件内存池（常驻: 48 x 最大事件尺寸，零碎片） */
    if (s_event_pool == NULL) {
        s_event_pool = mem_pool_create("evt",
                                        calc_event_total_size(EVENT_BUS_MAX_EVENT_SIZE),
                                        EVENT_POOL_BLOCKS, MEM_MOD_CORE);
        if (s_event_pool == NULL) {
            LOGE(TAG, "Failed to create event pool");
            return EVENT_BUS_ERR_NO_MEMORY;
        }
    }

    /* 创建事件队列 */
    ctx->queue = bus_queue_create(EVENT_BUS_QUEUE_SIZE, (int)sizeof(queue_node_t));
    if (ctx->queue == NULL) {
        LOGE(TAG, "Failed to create event queue");
        return EVENT_BUS_ERR_NO_MEMORY;
    }
    LOGI(TAG, "Event queue created, size=%d", EVENT_BUS_QUEUE_SIZE);

    /* 创建互斥锁 */
    ctx->lock = bus_lock_create();
    if (ctx->lock == NULL) {
        LOGE(TAG, "Failed to create lock");
        bus_queue_destroy(ctx->queue);
        ctx->queue = NULL;
        return EVENT_BUS_ERR_NO_MEMORY;
    }

    /* 初始化订阅者数组 */
    for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        ctx->subscribers[i].active = false;
        ctx->subscribers[i].id = EVENT_BUS_INVALID_SUB_ID;
    }

    /* 初始化订阅句柄池 */
    memset(s_subscription_pool_used, 0, sizeof(s_subscription_pool_used));

    /* 设置初始化标志 */
    ctx->initialized = true;
    ctx->next_subscriber_id = 1;  /* 从1开始，0表示无效 */
    atomic_store(&s_heap_fallback_count, 0);

    /* 创建事件处理任务 */
    if (!bus_task_create("event_bus", EVENT_BUS_TASK_STACK_SIZE,
                         EVENT_BUS_TASK_PRIORITY,
                         event_bus_process_task, &ctx->task_handle)) {
        LOGE(TAG, "Failed to create event bus task");
        bus_lock_destroy(ctx->lock);
        ctx->lock = NULL;
        bus_queue_destroy(ctx->queue);
        ctx->queue = NULL;
        ctx->initialized = false;
        return EVENT_BUS_ERR_NO_MEMORY;
    }

    LOGI(TAG, "Event bus initialized successfully");
    LOGI(TAG, "  Queue size: %d", EVENT_BUS_QUEUE_SIZE);
    LOGI(TAG, "  Max subscribers: %d", EVENT_BUS_MAX_SUBSCRIBERS);
    LOGI(TAG, "  Event pool: %d x %d bytes",
         EVENT_POOL_BLOCKS, (int)calc_event_total_size(EVENT_BUS_MAX_EVENT_SIZE));
    LOGI(TAG, "  Max event size: %d bytes", EVENT_BUS_MAX_EVENT_SIZE);
    LOGI(TAG, "  Task stack: %d bytes", EVENT_BUS_TASK_STACK_SIZE);

    return EVENT_BUS_OK;
}

/**
 * @brief 反初始化事件总线
 */
event_bus_err_t event_bus_deinit(void) {
    event_bus_context_t* ctx = event_bus_get_context();

    /* 检查是否已初始化 */
    if (!ctx->initialized) {
        return EVENT_BUS_ERR_NOT_INIT;
    }

    LOGI(TAG, "Deinitializing event bus...");

    /* 停止处理任务 */
    if (ctx->task_handle != NULL) {
        bus_task_delete(ctx->task_handle);
        ctx->task_handle = NULL;
    }

    /* 清空队列 */
    queue_node_t node;
    while (bus_queue_recv(ctx->queue, &node, 0)) {
        event_bus_destroy_event(node.event);
    }

    /* 删除队列与锁 */
    bus_queue_destroy(ctx->queue);
    ctx->queue = NULL;
    bus_lock_destroy(ctx->lock);
    ctx->lock = NULL;

    /* 清空订阅者 */
    for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        ctx->subscribers[i].active = false;
    }
    ctx->subscriber_count = 0;

    /* 标记为未初始化 */
    ctx->initialized = false;

    LOGI(TAG, "Event bus deinitialized");

    return EVENT_BUS_OK;
}

/**
 * @brief 发布事件
 */
event_bus_err_t event_bus_publish(event_type_t type, const void* data, size_t data_len) {
    event_bus_context_t* ctx = event_bus_get_context();

    /* 检查是否已初始化 */
    if (!ctx->initialized) {
        LOGE(TAG, "publish: event bus not initialized");
        return EVENT_BUS_ERR_NOT_INIT;
    }

    /* 参数检查 */
    if (data_len > EVENT_BUS_MAX_EVENT_SIZE) {
        LOGE(TAG, "publish: data_len %zu exceeds max %d", data_len, EVENT_BUS_MAX_EVENT_SIZE);
        return EVENT_BUS_ERR_INVALID_PARAM;
    }

    if (data_len > 0 && data == NULL) {
        LOGE(TAG, "publish: data is NULL but data_len > 0");
        return EVENT_BUS_ERR_INVALID_PARAM;
    }

    /* 创建事件对象 */
    event_t* event = event_bus_create_event(type, data, (uint16_t)data_len);
    if (event == NULL) {
        LOGE(TAG, "publish: failed to create event");
        return EVENT_BUS_ERR_NO_MEMORY;
    }

    /* 构造队列节点 */
    queue_node_t node = {
        .event = event,
        .total_size = calc_event_total_size((uint16_t)data_len)
    };

    /* 发送到队列 */
    if (!bus_queue_send(ctx->queue, &node)) {
        LOGW(TAG, "publish: queue full, dropping event 0x%08X", type);
        event_bus_destroy_event(event);
        atomic_fetch_add(&ctx->stats.events_dropped, 1);
        return EVENT_BUS_ERR_QUEUE_FULL;
    }

    /* 更新统计信息 */
    atomic_fetch_add(&ctx->stats.events_published, 1);

    LOGD(TAG, "publish: event 0x%08X, data_len=%zu", type, data_len);

    return EVENT_BUS_OK;
}

/**
 * @brief 从池中分配订阅句柄
 */
static event_subscription_t* alloc_subscription(void) {
    for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        if (!s_subscription_pool_used[i]) {
            s_subscription_pool_used[i] = true;
            return &s_subscription_pool[i];
        }
    }
    return NULL;
}

/**
 * @brief 释放订阅句柄到池
 */
static void free_subscription(event_subscription_t* sub) {
    if (sub != NULL) {
        size_t index = sub - s_subscription_pool;
        if (index < EVENT_BUS_MAX_SUBSCRIBERS) {
            s_subscription_pool_used[index] = false;
        }
    }
}

/**
 * @brief 订阅事件
 */
event_subscription_t* event_bus_subscribe(event_type_t event_type,
                                          event_handler_fn_t handler,
                                          void* user_data) {
    event_bus_context_t* ctx = event_bus_get_context();

    /* 检查是否已初始化 */
    if (!ctx->initialized) {
        LOGE(TAG, "subscribe: event bus not initialized");
        return NULL;
    }

    /* 参数检查 */
    if (handler == NULL) {
        LOGE(TAG, "subscribe: handler is NULL");
        return NULL;
    }

    /* 获取互斥锁 */
    if (!bus_lock_take(ctx->lock, 1000)) {
        LOGE(TAG, "subscribe: failed to take lock");
        return NULL;
    }

    /* 检查订阅者数量 */
    if (ctx->subscriber_count >= EVENT_BUS_MAX_SUBSCRIBERS) {
        LOGE(TAG, "subscribe: max subscribers reached (%d)", EVENT_BUS_MAX_SUBSCRIBERS);
        bus_lock_give(ctx->lock);
        return NULL;
    }

    /* 查找空闲槽位 */
    int slot = -1;
    for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        if (!ctx->subscribers[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        LOGE(TAG, "subscribe: no free slot found");
        bus_lock_give(ctx->lock);
        return NULL;
    }

    /* 分配订阅句柄 */
    event_subscription_t* sub = alloc_subscription();
    if (sub == NULL) {
        LOGE(TAG, "subscribe: failed to allocate subscription handle");
        bus_lock_give(ctx->lock);
        return NULL;
    }

    /* 填充订阅者信息 */
    subscriber_t* subscriber = &ctx->subscribers[slot];
    subscriber->event_type = event_type;
    subscriber->handler = handler;
    subscriber->user_data = user_data;
    subscriber->id = ctx->next_subscriber_id++;
    subscriber->active = true;

    /* 填充订阅句柄 */
    sub->subscriber_id = subscriber->id;
    sub->event_type = event_type;

    /* 更新计数 */
    ctx->subscriber_count++;

    /* 释放互斥锁 */
    bus_lock_give(ctx->lock);

    LOGI(TAG, "subscribe: event 0x%08X, id=%lu, total=%d",
         event_type, (unsigned long)subscriber->id, ctx->subscriber_count);

    return sub;
}

/**
 * @brief 取消订阅
 */
event_bus_err_t event_bus_unsubscribe(event_subscription_t* subscription) {
    event_bus_context_t* ctx = event_bus_get_context();

    /* 检查是否已初始化 */
    if (!ctx->initialized) {
        return EVENT_BUS_ERR_NOT_INIT;
    }

    /* 参数检查 */
    if (subscription == NULL) {
        LOGE(TAG, "unsubscribe: subscription is NULL");
        return EVENT_BUS_ERR_INVALID_PARAM;
    }

    /* 获取互斥锁 */
    if (!bus_lock_take(ctx->lock, 1000)) {
        LOGE(TAG, "unsubscribe: failed to take lock");
        return EVENT_BUS_ERR_TIMEOUT;
    }

    /* 查找订阅者 */
    bool found = false;
    for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        subscriber_t* sub = &ctx->subscribers[i];

        if (sub->active && sub->id == subscription->subscriber_id) {
            /* 找到订阅者，标记为非活跃 */
            sub->active = false;
            sub->handler = NULL;
            sub->user_data = NULL;
            ctx->subscriber_count--;
            found = true;

            LOGI(TAG, "unsubscribe: id=%lu, remaining=%d",
                 (unsigned long)subscription->subscriber_id, ctx->subscriber_count);
            break;
        }
    }

    /* 释放互斥锁 */
    bus_lock_give(ctx->lock);

    /* 释放订阅句柄 */
    free_subscription(subscription);

    if (!found) {
        LOGW(TAG, "unsubscribe: subscriber id=%lu not found",
             (unsigned long)subscription->subscriber_id);
        return EVENT_BUS_ERR_NOT_FOUND;
    }

    return EVENT_BUS_OK;
}

/**
 * @brief 获取事件总线状态
 */
event_bus_err_t event_bus_get_status(int* queue_size, int* subscriber_count) {
    event_bus_context_t* ctx = event_bus_get_context();

    if (!ctx->initialized) {
        return EVENT_BUS_ERR_NOT_INIT;
    }

    if (queue_size != NULL) {
        *queue_size = bus_queue_count(ctx->queue);
    }

    if (subscriber_count != NULL) {
        *subscriber_count = ctx->subscriber_count;
    }

    return EVENT_BUS_OK;
}

/**
 * @brief 获取事件总线统计信息
 */
event_bus_err_t event_bus_get_stats(uint32_t* events_published,
                                    uint32_t* events_processed,
                                    uint32_t* events_dropped,
                                    uint32_t* handler_errors) {
    event_bus_context_t* ctx = event_bus_get_context();

    if (!ctx->initialized) {
        return EVENT_BUS_ERR_NOT_INIT;
    }

    if (events_published != NULL) {
        *events_published = atomic_load(&ctx->stats.events_published);
    }

    if (events_processed != NULL) {
        *events_processed = atomic_load(&ctx->stats.events_processed);
    }

    if (events_dropped != NULL) {
        *events_dropped = atomic_load(&ctx->stats.events_dropped);
    }

    if (handler_errors != NULL) {
        *handler_errors = atomic_load(&ctx->stats.handler_errors);
    }

    return EVENT_BUS_OK;
}

/**
 * @brief 重置统计信息
 */
event_bus_err_t event_bus_reset_stats(void) {
    event_bus_context_t* ctx = event_bus_get_context();

    if (!ctx->initialized) {
        return EVENT_BUS_ERR_NOT_INIT;
    }

    atomic_store(&ctx->stats.events_published, 0);
    atomic_store(&ctx->stats.events_processed, 0);
    atomic_store(&ctx->stats.events_dropped, 0);
    atomic_store(&ctx->stats.handler_errors, 0);

    return EVENT_BUS_OK;
}

/**
 * @brief 检查事件总线是否已初始化
 */
bool event_bus_is_initialized(void) {
    event_bus_context_t* ctx = event_bus_get_context();
    return ctx->initialized;
}

uint32_t event_bus_get_heap_fallback(void) {
    return atomic_load(&s_heap_fallback_count);
}

/* ========================================================================== */
/*                              调试辅助实现                                   */
/* ========================================================================== */

/**
 * @brief 事件类型名称映射表（由 event_bus_types.h 枚举全量生成，勿手工增删）
 */
static const struct {
    event_type_t type;
    const char* name;
} s_event_type_names[] = {
    { EVENT_SYSTEM_STARTUP, "SYSTEM_STARTUP" },
    { EVENT_SYSTEM_SHUTDOWN, "SYSTEM_SHUTDOWN" },
    { EVENT_SYSTEM_ERROR, "SYSTEM_ERROR" },
    { EVENT_SYSTEM_REBOOT, "SYSTEM_REBOOT" },
    { EVENT_SYSTEM_WATCHDOG_FEED, "SYSTEM_WATCHDOG_FEED" },
    { EVENT_WIFI_CONNECTED, "WIFI_CONNECTED" },
    { EVENT_WIFI_DISCONNECTED, "WIFI_DISCONNECTED" },
    { EVENT_WIFI_SCAN_DONE, "WIFI_SCAN_DONE" },
    { EVENT_WIFI_SWITCH_START, "WIFI_SWITCH_START" },
    { EVENT_WIFI_SWITCH_DONE, "WIFI_SWITCH_DONE" },
    { EVENT_WIFI_SWITCH_FAILED, "WIFI_SWITCH_FAILED" },
    { EVENT_WIFI_GOT_IP, "WIFI_GOT_IP" },
    { EVENT_WIFI_AP_STARTED, "WIFI_AP_STARTED" },
    { EVENT_WIFI_AP_STOPPED, "WIFI_AP_STOPPED" },
    { EVENT_WIFI_MODE_CHANGED, "WIFI_MODE_CHANGED" },
    { EVENT_SENSOR_TEMP_HUMIDITY, "SENSOR_TEMP_HUMIDITY" },
    { EVENT_SENSOR_ERROR, "SENSOR_ERROR" },
    { EVENT_TOUCH_PRESS, "TOUCH_PRESS" },
    { EVENT_TOUCH_RELEASE, "TOUCH_RELEASE" },
    { EVENT_TOUCH_SWIPE, "TOUCH_SWIPE" },
    { EVENT_TOUCH_LONG_PRESS, "TOUCH_LONG_PRESS" },
    { EVENT_LORA_DATA_RECEIVED, "LORA_DATA_RECEIVED" },
    { EVENT_LORA_SEND_COMPLETE, "LORA_SEND_COMPLETE" },
    { EVENT_LORA_SEND_FAILED, "LORA_SEND_FAILED" },
    { EVENT_LORA_READY, "LORA_READY" },
    { EVENT_UI_PAGE_CHANGE, "UI_PAGE_CHANGE" },
    { EVENT_UI_ANIMATION_DONE, "UI_ANIMATION_DONE" },
    { EVENT_UI_REFRESH_REQUEST, "UI_REFRESH_REQUEST" },
    { EVENT_AUDIO_PLAY_START, "AUDIO_PLAY_START" },
    { EVENT_AUDIO_PLAY_DONE, "AUDIO_PLAY_DONE" },
    { EVENT_AUDIO_RECORD_START, "AUDIO_RECORD_START" },
    { EVENT_AUDIO_RECORD_DONE, "AUDIO_RECORD_DONE" },
    { EVENT_AUDIO_DATA, "AUDIO_DATA" },
    { EVENT_AUDIO_READY, "AUDIO_READY" },
    { EVENT_STORAGE_OPERATION_DONE, "STORAGE_OPERATION_DONE" },
    { EVENT_STORAGE_ERROR, "STORAGE_ERROR" },
    { EVENT_STORAGE_READY, "STORAGE_READY" },
    { EVENT_DISPLAY_REFRESH_DONE, "DISPLAY_REFRESH_DONE" },
    { EVENT_DISPLAY_BACKLIGHT_CHANGE, "DISPLAY_BACKLIGHT_CHANGE" },
    { EVENT_DISPLAY_READY, "DISPLAY_READY" },
    { EVENT_WEB_WS_CLIENT_CONNECTED, "WEB_WS_CLIENT_CONNECTED" },
    { EVENT_WEB_WS_CLIENT_DISCONNECTED, "WEB_WS_CLIENT_DISCONNECTED" },
    { EVENT_WEB_REQUEST_DONE, "WEB_REQUEST_DONE" },
    { EVENT_TYPE_MAX, NULL }
};

/**
 * @brief 获取事件类型名称
 */
const char* event_bus_get_type_name(event_type_t type) {
    for (int i = 0; s_event_type_names[i].name != NULL; i++) {
        if (s_event_type_names[i].type == type) {
            return s_event_type_names[i].name;
        }
    }
    return "UNKNOWN";
}

/**
 * @brief 获取错误码名称
 */
const char* event_bus_get_err_name(event_bus_err_t err) {
    switch (err) {
        case EVENT_BUS_OK:                  return "OK";
        case EVENT_BUS_ERR_INVALID_PARAM:   return "INVALID_PARAM";
        case EVENT_BUS_ERR_NOT_INIT:        return "NOT_INIT";
        case EVENT_BUS_ERR_ALREADY_INIT:    return "ALREADY_INIT";
        case EVENT_BUS_ERR_QUEUE_FULL:      return "QUEUE_FULL";
        case EVENT_BUS_ERR_NO_MEMORY:       return "NO_MEMORY";
        case EVENT_BUS_ERR_NOT_FOUND:       return "NOT_FOUND";
        case EVENT_BUS_ERR_MAX_SUBSCRIBERS: return "MAX_SUBSCRIBERS";
        case EVENT_BUS_ERR_TIMEOUT:         return "TIMEOUT";
        case EVENT_BUS_ERR_INTERNAL:        return "INTERNAL";
        default:                            return "UNKNOWN";
    }
}

/**
 * @brief 打印事件总线状态
 */
void event_bus_print_status(void) {
    event_bus_context_t* ctx = event_bus_get_context();

    printf("\n");
    printf("=== Event Bus Status ===\n");
    printf("Initialized: %s\n", ctx->initialized ? "Yes" : "No");

    if (ctx->initialized) {
        int queue_size = 0;
        int subscriber_count = 0;
        event_bus_get_status(&queue_size, &subscriber_count);

        printf("Queue: %d / %d\n", queue_size, EVENT_BUS_QUEUE_SIZE);
        printf("Subscribers: %d / %d\n", subscriber_count, EVENT_BUS_MAX_SUBSCRIBERS);
        printf("Stats:\n");
        printf("  Published:  %lu\n", (unsigned long)atomic_load(&ctx->stats.events_published));
        printf("  Processed:  %lu\n", (unsigned long)atomic_load(&ctx->stats.events_processed));
        printf("  Dropped:    %lu\n", (unsigned long)atomic_load(&ctx->stats.events_dropped));
        printf("  Errors:     %lu\n", (unsigned long)atomic_load(&ctx->stats.handler_errors));
        printf("  HeapFallback: %lu\n", (unsigned long)atomic_load(&s_heap_fallback_count));
    }
    printf("========================\n\n");
}

/**
 * @brief 打印所有订阅者信息
 */
void event_bus_print_subscribers(void) {
    event_bus_context_t* ctx = event_bus_get_context();

    printf("\n");
    printf("=== Event Bus Subscribers ===\n");

    if (!ctx->initialized) {
        printf("Not initialized\n");
        printf("=============================\n\n");
        return;
    }

    /* 获取互斥锁 */
    if (!bus_lock_take(ctx->lock, 1000)) {
        printf("Failed to access subscribers\n");
        printf("=============================\n\n");
        return;
    }

    int active_count = 0;
    for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        subscriber_t* sub = &ctx->subscribers[i];

        if (sub->active) {
            printf("[%d] ID=%lu, Event=0x%08X (%s), Handler=%p\n",
                   i, (unsigned long)sub->id, sub->event_type,
                   event_bus_get_type_name(sub->event_type),
                   (void*)sub->handler);
            active_count++;
        }
    }

    if (active_count == 0) {
        printf("No active subscribers\n");
    } else {
        printf("Total: %d active subscribers\n", active_count);
    }

    /* 释放互斥锁 */
    bus_lock_give(ctx->lock);

    printf("=============================\n\n");
}
