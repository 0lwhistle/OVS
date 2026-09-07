/**
 * @file lvgl.h
 * @brief LVGL库头文件（临时占位）
 * 
 * 这是一个临时占位文件，用于解决编译问题。
 * 实际的LVGL库需要单独添加。
 */

#ifndef LVGL_H
#define LVGL_H

#include <stdint.h>
#include <stdbool.h>

/* 临时定义，实际实现需要LVGL库 */
typedef struct _lv_obj_t lv_obj_t;
typedef struct _lv_timer_t lv_timer_t;

/* 临时函数声明 */
static inline void lv_init(void) {}
static inline lv_obj_t* lv_scr_act(void) { return NULL; }
static inline void lv_timer_handler(void) {}

#endif /* LVGL_H */
