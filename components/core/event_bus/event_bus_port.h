/**
 * @file event_bus_port.h
 * @brief 事件总线平台移植层（内部头，仅 event_bus.c 使用）
 *
 * ESP:   FreeRTOS 队列/互斥信号量/任务（与历史行为一致）
 * PC:    pthread 有界环形队列 + 互斥量/条件变量（ovs_tests 门禁用）
 *
 * 语义约定:
 * - bus_queue_send 非阻塞，满则返回 false（发布方丢弃计数）
 * - bus_lock_take 带 超时（ms），超时返回 false
 * - bus_task_create 成功返回 true，句柄存 *handle_out
 */

#ifndef EVENT_BUS_PORT_H
#define EVENT_BUS_PORT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bus_queue bus_queue_t;
typedef struct bus_lock bus_lock_t;

/* ---------- 队列 ---------- */
bus_queue_t* bus_queue_create(int depth, int item_size);   /* 拷贝语义（同 FreeRTOS 队列） */
bool         bus_queue_send(bus_queue_t* q, void* item);
bool         bus_queue_recv(bus_queue_t* q, void* item, int timeout_ms);
int          bus_queue_count(bus_queue_t* q);
void         bus_queue_destroy(bus_queue_t* q);

/* ---------- 互斥锁 ---------- */
bus_lock_t* bus_lock_create(void);
bool        bus_lock_take(bus_lock_t* lock, int timeout_ms);
void        bus_lock_give(bus_lock_t* lock);
void        bus_lock_destroy(bus_lock_t* lock);

/* ---------- 任务 ---------- */
bool bus_task_create(const char* name, int stack_size, int prio,
                     void (*fn)(void*), void** handle_out);
void bus_task_delete(void* handle);

/* ---------- 时基 ---------- */
uint32_t bus_now_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_BUS_PORT_H */
