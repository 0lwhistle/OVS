/**
 * @file esp_timer.h
 * @brief PC 测试垫片：esp_timer_get_time（ovs_tests 专用）
 */
#ifndef ESP_TIMER_SHIM_H
#define ESP_TIMER_SHIM_H
#include <time.h>
static inline long long esp_timer_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}
#endif
