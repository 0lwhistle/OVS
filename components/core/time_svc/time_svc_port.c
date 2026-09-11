/**
 * @file time_svc_port.c
 * @brief 时间服务平台移植层实现
 *
 * ESP: esp_timer_get_time；PC: CLOCK_MONOTONIC（POSIX）
 */

#include "time_svc_port.h"

#if defined(ESP_PLATFORM)

#include "esp_timer.h"

uint64_t time_svc_port_tick_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

#else /* PC */

#include <time.h>

uint64_t time_svc_port_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

#endif /* ESP_PLATFORM */
