/**
 * @file FreeRTOS.h
 * @brief PC 测试垫片：ath30.c 用的 FreeRTOS 宏/函数（ovs_tests 专用）
 */
#ifndef FREERTOS_SHIM_H
#define FREERTOS_SHIM_H
#define pdMS_TO_TICKS(x) (x)
static inline void vTaskDelay(unsigned int ticks) { (void)ticks; }
#endif
