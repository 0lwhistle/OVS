/**
 * @file bridge_time_sim.c
 * @brief 时间桥接 PC 模拟器实现：真实系统本地时间
 */

#include "bridge_time.h"
#include <time.h>

void bridge_time_get(uint8_t* hour, uint8_t* min, uint8_t* sec) {
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    if (hour) {
        *hour = (uint8_t)lt.tm_hour;
    }
    if (min) {
        *min = (uint8_t)lt.tm_min;
    }
    if (sec) {
        *sec = (uint8_t)lt.tm_sec;
    }
}
