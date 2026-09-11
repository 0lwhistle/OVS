/**
 * @file sensor_cache_port.c
 * @brief 传感器缓存平台移植层实现
 *
 * ESP: FreeRTOS 互斥信号量 + esp_timer；PC: pthread 互斥量 + CLOCK_MONOTONIC
 * （与 audio_port.c 同风格，业务 .c 零条件编译）
 */

#include "sensor_cache_port.h"

#include <stdlib.h>

#if defined(ESP_PLATFORM)

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

struct sensor_lock {
    SemaphoreHandle_t h;
};

sensor_lock_t* sensor_lock_create(void) {
    sensor_lock_t* l = (sensor_lock_t*)malloc(sizeof(sensor_lock_t));
    if (!l) {
        return NULL;
    }
    l->h = xSemaphoreCreateMutex();
    if (!l->h) {
        free(l);
        return NULL;
    }
    return l;
}

bool sensor_lock_take(sensor_lock_t* lock, uint32_t timeout_ms) {
    return lock && xSemaphoreTake(lock->h, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void sensor_lock_give(sensor_lock_t* lock) {
    if (lock) {
        xSemaphoreGive(lock->h);
    }
}

void sensor_lock_destroy(sensor_lock_t* lock) {
    if (lock) {
        vSemaphoreDelete(lock->h);
        free(lock);
    }
}

uint64_t sensor_tick_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

#else /* PC */

#include <pthread.h>
#include <time.h>
#include <errno.h>

struct sensor_lock {
    pthread_mutex_t m;
};

sensor_lock_t* sensor_lock_create(void) {
    sensor_lock_t* l = (sensor_lock_t*)malloc(sizeof(sensor_lock_t));
    if (!l) {
        return NULL;
    }
    if (pthread_mutex_init(&l->m, NULL) != 0) {
        free(l);
        return NULL;
    }
    return l;
}

bool sensor_lock_take(sensor_lock_t* lock, uint32_t timeout_ms) {
    if (!lock) {
        return false;
    }
    if (timeout_ms == 0) {
        return pthread_mutex_trylock(&lock->m) == 0;
    }
    /* PC 侧无严格超时需求，阻塞取（tests 单线程/短临界区） */
    (void)timeout_ms;
    return pthread_mutex_lock(&lock->m) == 0;
}

void sensor_lock_give(sensor_lock_t* lock) {
    if (lock) {
        pthread_mutex_unlock(&lock->m);
    }
}

void sensor_lock_destroy(sensor_lock_t* lock) {
    if (lock) {
        pthread_mutex_destroy(&lock->m);
        free(lock);
    }
}

uint64_t sensor_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

#endif /* ESP_PLATFORM */
