/**
 * @file event_bus_port.c
 * @brief 事件总线平台移植层实现
 *
 * ESP: FreeRTOS 队列/互斥信号量/xTask（行为与历史实现一致）
 * PC:  pthread 有界环形队列（mutex+condvar）/pthread 互斥量/pthread 线程
 */

#include "event_bus_port.h"

#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM)

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

struct bus_queue { QueueHandle_t h; };
struct bus_lock  { SemaphoreHandle_t h; };

bus_queue_t* bus_queue_create(int depth, int item_size) {
    bus_queue_t* q = (bus_queue_t*)malloc(sizeof(bus_queue_t));
    if (!q) return NULL;
    q->h = xQueueCreate(depth, (unsigned)item_size);
    if (!q->h) { free(q); return NULL; }
    return q;
}

bool bus_queue_send(bus_queue_t* q, void* item) {
    /* item 即元素地址：xQueue 按 item_size 拷贝元素内容（与 PC 分支语义一致） */
    return q && xQueueSend(q->h, item, 0) == pdTRUE;
}

bool bus_queue_recv(bus_queue_t* q, void* item, int timeout_ms) {
    return q && xQueueReceive(q->h, item, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

int bus_queue_count(bus_queue_t* q) {
    return q ? (int)uxQueueMessagesWaiting(q->h) : 0;
}

void bus_queue_destroy(bus_queue_t* q) {
    if (q) { vQueueDelete(q->h); free(q); }
}

bus_lock_t* bus_lock_create(void) {
    bus_lock_t* l = (bus_lock_t*)malloc(sizeof(bus_lock_t));
    if (!l) return NULL;
    l->h = xSemaphoreCreateMutex();
    if (!l->h) { free(l); return NULL; }
    return l;
}

bool bus_lock_take(bus_lock_t* lock, int timeout_ms) {
    return lock && xSemaphoreTake(lock->h, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void bus_lock_give(bus_lock_t* lock) {
    if (lock) xSemaphoreGive(lock->h);
}

void bus_lock_destroy(bus_lock_t* lock) {
    if (lock) { vSemaphoreDelete(lock->h); free(lock); }
}

bool bus_task_create(const char* name, int stack_size, int prio,
                     void (*fn)(void*), void** handle_out) {
    return xTaskCreatePinnedToCore(fn, name, stack_size, NULL, prio,
                                   (TaskHandle_t*)handle_out,
                                   tskNO_AFFINITY) == pdPASS;
}

void bus_task_delete(void* handle) {
    if (handle) vTaskDelete((TaskHandle_t)handle);
}

uint32_t bus_now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

#else /* !ESP_PLATFORM —— PC（pthread） */

#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

struct bus_queue {
    uint8_t*      buf;          /* 元素按 item_size 拷贝存储（同 FreeRTOS 语义） */
    int           depth;
    int           item_size;
    int           head;
    int           tail;
    int           count;
    pthread_mutex_t mtx;
    pthread_cond_t  not_empty;
};

struct bus_lock { pthread_mutex_t mtx; };

bus_queue_t* bus_queue_create(int depth, int item_size) {
    bus_queue_t* q = (bus_queue_t*)calloc(1, sizeof(bus_queue_t));
    if (!q) return NULL;
    q->buf = (uint8_t*)calloc(depth, item_size);
    if (!q->buf) { free(q); return NULL; }
    q->depth = depth;
    q->item_size = item_size;
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    return q;
}

bool bus_queue_send(bus_queue_t* q, void* item) {
    if (!q) return false;
    bool ok = false;
    pthread_mutex_lock(&q->mtx);
    if (q->count < q->depth) {
        memcpy(q->buf + (size_t)q->head * q->item_size, item, q->item_size);
        q->head = (q->head + 1) % q->depth;
        q->count++;
        ok = true;
        pthread_cond_signal(&q->not_empty);
    }
    pthread_mutex_unlock(&q->mtx);
    return ok;
}

bool bus_queue_recv(bus_queue_t* q, void* item, int timeout_ms) {
    if (!q) return false;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }

    bool ok = false;
    pthread_mutex_lock(&q->mtx);
    while (q->count == 0) {
        if (pthread_cond_timedwait(&q->not_empty, &q->mtx, &ts) == ETIMEDOUT) {
            break;
        }
    }
    if (q->count > 0) {
        memcpy(item, q->buf + (size_t)q->tail * q->item_size, q->item_size);
        q->tail = (q->tail + 1) % q->depth;
        q->count--;
        ok = true;
    }
    pthread_mutex_unlock(&q->mtx);
    return ok;
}

int bus_queue_count(bus_queue_t* q) {
    if (!q) return 0;
    pthread_mutex_lock(&q->mtx);
    int n = q->count;
    pthread_mutex_unlock(&q->mtx);
    return n;
}

void bus_queue_destroy(bus_queue_t* q) {
    if (!q) return;
    pthread_mutex_destroy(&q->mtx);
    pthread_cond_destroy(&q->not_empty);
    free(q->buf);
    free(q);
}

bus_lock_t* bus_lock_create(void) {
    bus_lock_t* l = (bus_lock_t*)malloc(sizeof(bus_lock_t));
    if (!l) return NULL;
    pthread_mutex_init(&l->mtx, NULL);
    return l;
}

bool bus_lock_take(bus_lock_t* lock, int timeout_ms) {
    if (!lock) return false;
    if (timeout_ms < 0) {                       /* 永等 */
        return pthread_mutex_lock(&lock->mtx) == 0;
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    /* PC 测试场景互斥竞争极轻，trylock 轮询足够 */
    while (pthread_mutex_trylock(&lock->mtx) == EBUSY) {
        struct timespec s = { .tv_sec = 0, .tv_nsec = 200000 };
        nanosleep(&s, NULL);
        if (--timeout_ms < 0) return false;
    }
    return true;
}

void bus_lock_give(bus_lock_t* lock) {
    if (lock) pthread_mutex_unlock(&lock->mtx);
}

void bus_lock_destroy(bus_lock_t* lock) {
    if (lock) { pthread_mutex_destroy(&lock->mtx); free(lock); }
}

typedef struct {
    pthread_t       tid;
    void (*fn)(void*);
    bool            alive;
} bus_task_pc_t;

static void* pc_task_trampoline(void* arg) {
    bus_task_pc_t* t = (bus_task_pc_t*)arg;
    t->fn(t);
    t->alive = false;
    return NULL;
}

bool bus_task_create(const char* name, int stack_size, int prio,
                     void (*fn)(void*), void** handle_out) {
    (void)name; (void)stack_size; (void)prio;
    bus_task_pc_t* t = (bus_task_pc_t*)calloc(1, sizeof(bus_task_pc_t));
    if (!t) return false;
    t->fn = fn;
    t->alive = true;
    if (pthread_create(&t->tid, NULL, pc_task_trampoline, t) != 0) {
        free(t);
        return false;
    }
    /* 句柄即任务结构本身；delete 语义 = detach（事件总线任务常驻不退出） */
    if (handle_out) *handle_out = t;
    return true;
}

void bus_task_delete(void* handle) {
    /* 事件总线常驻任务不主动 join；进程退出由测试框架控制 */
    (void)handle;
}

uint32_t bus_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

#endif /* ESP_PLATFORM */
