/**
 * @file tasker_port.c
 * @brief tasker 平台移植层实现
 */

#include "tasker_port.h"

#if defined(ESP_PLATFORM)

uint64_t tasker_now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

int tasker_timer_create(tasker_timer_t* timer, void (*cb)(void*), void* arg) {
    const esp_timer_create_args_t args = {
        .callback = cb,
        .arg = arg,
        .name = "tasker_timeout"
    };
    return esp_timer_create(&args, timer) == ESP_OK ? 0 : -1;
}

int tasker_timer_start_once(tasker_timer_t timer, uint64_t us) {
    return esp_timer_start_once(timer, us) == ESP_OK ? 0 : -1;
}

void tasker_timer_stop(tasker_timer_t timer) {
    esp_timer_stop(timer);
}

void tasker_timer_delete(tasker_timer_t timer) {
    esp_timer_delete(timer);
}

#else /* !ESP_PLATFORM —— PC */

#include <time.h>
#include <stdlib.h>

typedef struct {
    void (*cb)(void*);
    void* arg;
} tasker_timer_pc_t;

uint64_t tasker_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

int tasker_timer_create(tasker_timer_t* timer, void (*cb)(void*), void* arg) {
    (void)cb; (void)arg;
    *timer = NULL;          /* PC: 超时检测不启用 */
    return 0;
}

int tasker_timer_start_once(tasker_timer_t timer, uint64_t us) {
    (void)timer; (void)us;
    return 0;
}

void tasker_timer_stop(tasker_timer_t timer) {
    (void)timer;
}

void tasker_timer_delete(tasker_timer_t timer) {
    tasker_timer_pc_t* t = (tasker_timer_pc_t*)timer;
    free(t);
}

#endif /* ESP_PLATFORM */
