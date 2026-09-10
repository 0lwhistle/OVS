/**
 * @file audio_port.c
 * @brief 音频模块平台移植层实现
 *
 * ESP: FreeRTOS 互斥信号量/xTask/vTaskDelay + esp_timer + gpio_ctrl 驱动
 * PC:  pthread 互斥量/线程 + nanosleep + clock_gettime（SD 脚空操作）
 */

#include "audio_port.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static const char* TAG = "[AUDIO_PORT]";

#if defined(ESP_PLATFORM)

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "gpio_ctrl.h"

struct audio_lock {
    SemaphoreHandle_t h;
};

audio_lock_t* audio_lock_create(void) {
    audio_lock_t* l = (audio_lock_t*)malloc(sizeof(audio_lock_t));
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

bool audio_lock_take(audio_lock_t* lock, uint32_t timeout_ms) {
    return lock && xSemaphoreTake(lock->h, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void audio_lock_give(audio_lock_t* lock) {
    if (lock) {
        xSemaphoreGive(lock->h);
    }
}

void audio_lock_destroy(audio_lock_t* lock) {
    if (lock) {
        vSemaphoreDelete(lock->h);
        free(lock);
    }
}

bool audio_task_create(const char* name, int stack_size, int prio,
                       void (*fn)(void*), void* arg, void** handle_out) {
    BaseType_t ret = xTaskCreate(fn, name, (uint16_t)stack_size, arg,
                                 (UBaseType_t)prio, (TaskHandle_t*)handle_out);
    return ret == pdPASS;
}

void audio_task_delete(void* handle) {
    if (handle) {
        vTaskDelete((TaskHandle_t)handle);
    } else {
        vTaskDelete(NULL);   /* 自删除 */
    }
}

void audio_sleep_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint32_t audio_now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

bool audio_port_sd_init(int32_t pin, bool active_high) {
    if (pin < 0) {
        return false;
    }
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << (uint32_t)pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    /* 初始电平=关断（静音），上电期间不炸耳 */
    gpio_ctrl_init((uint8_t)pin, active_high ? 0 : 1, io_conf);
    LOGI(TAG, "SD pin %" PRId32 " initialized (active %s)", pin, active_high ? "high" : "low");
    return true;
}

bool audio_port_sd_write(int32_t pin, bool level) {
    if (pin < 0) {
        return false;
    }
    return gpio_ctrl_set((uint8_t)pin, level ? 1 : 0) == GPIO_OK;
}

#else /* PC（pthread） */

#include <pthread.h>
#include <time.h>

struct audio_lock {
    pthread_mutex_t m;
};

audio_lock_t* audio_lock_create(void) {
    audio_lock_t* l = (audio_lock_t*)malloc(sizeof(audio_lock_t));
    if (!l) {
        return NULL;
    }
    if (pthread_mutex_init(&l->m, NULL) != 0) {
        free(l);
        return NULL;
    }
    return l;
}

bool audio_lock_take(audio_lock_t* lock, uint32_t timeout_ms) {
    (void)timeout_ms;   /* 测试场景不持锁跨阻塞点，简单阻塞即可 */
    return lock && pthread_mutex_lock(&lock->m) == 0;
}

void audio_lock_give(audio_lock_t* lock) {
    if (lock) {
        pthread_mutex_unlock(&lock->m);
    }
}

void audio_lock_destroy(audio_lock_t* lock) {
    if (lock) {
        pthread_mutex_destroy(&lock->m);
        free(lock);
    }
}

typedef struct {
    void (*fn)(void*);
    void* arg;
} audio_thread_arg_t;

static void* audio_thread_tramp(void* p) {
    audio_thread_arg_t ta = *(audio_thread_arg_t*)p;
    free(p);
    ta.fn(ta.arg);
    return NULL;
}

bool audio_task_create(const char* name, int stack_size, int prio,
                       void (*fn)(void*), void* arg, void** handle_out) {
    (void)stack_size;
    (void)prio;
    audio_thread_arg_t* ta = (audio_thread_arg_t*)malloc(sizeof(audio_thread_arg_t));
    if (!ta) {
        return false;
    }
    ta->fn = fn;
    ta->arg = arg;
    pthread_t* t = (pthread_t*)malloc(sizeof(pthread_t));
    if (!t) {
        free(ta);
        return false;
    }
    if (pthread_create(t, NULL, audio_thread_tramp, ta) != 0) {
        free(ta);
        free(t);
        LOGW(TAG, "task '%s' create failed", name ? name : "?");
        return false;
    }
    pthread_detach(*t);
    if (handle_out) {
        *handle_out = t;
    }
    return true;
}

void audio_task_delete(void* handle) {
    /* PC 测试进程生命周期=测试用例生命周期，不做强杀；自删除=结束线程函数 */
    if (handle) {
        free(handle);
    }
    pthread_exit(NULL);
}

void audio_sleep_ms(uint32_t ms) {
    struct timespec ts = {
        .tv_sec = (time_t)(ms / 1000u),
        .tv_nsec = (long)(ms % 1000u) * 1000000L,
    };
    nanosleep(&ts, NULL);
}

uint32_t audio_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

bool audio_port_sd_init(int32_t pin, bool active_high) {
    (void)active_high;
    if (pin < 0) {
        return false;
    }
    LOGI(TAG, "SD pin %" PRId32 " (PC stub, no gpio)", pin);
    return true;
}

bool audio_port_sd_write(int32_t pin, bool level) {
    if (pin < 0) {
        return false;
    }
    LOGD(TAG, "SD pin %" PRId32 " -> %d (PC stub)", pin, level ? 1 : 0);
    return true;
}

#endif /* ESP_PLATFORM */
