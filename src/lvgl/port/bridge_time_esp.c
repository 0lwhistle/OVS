/**
 * @file bridge_time_esp.c
 * @brief 时间桥接 ESP 实现（骨架：开机 uptime 模拟时钟）
 * TODO: time_srv（SNTP）就绪后替换为真实时间
 */

#include "bridge_time.h"
#include "esp_timer.h"

void bridge_time_get(uint8_t* hour, uint8_t* min, uint8_t* sec) {
    int64_t total = esp_timer_get_time() / 1000000LL;   /* 开机秒数 */
    if (hour) {
        *hour = (uint8_t)((total / 3600) % 24);
    }
    if (min) {
        *min = (uint8_t)((total / 60) % 60);
    }
    if (sec) {
        *sec = (uint8_t)(total % 60);
    }
}
