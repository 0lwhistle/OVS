/**
 * @file page_standby.h
 * @brief 页面层：待机占位页（I6 待机画面的默认内容）
 *
 * 未来任意画面可经 navigator_set_standby(cb) 注入替换本页内容。
 */

#ifndef PAGE_STANDBY_H
#define PAGE_STANDBY_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** screen_create_cb 兼容：待机覆盖层内容构建函数 */
lv_obj_t* page_standby_screen_create(lv_obj_t* parent);

/** presenter 注入：刷新待机时钟（外部 1s timer 驱动或自建） */
void page_standby_set_time(uint8_t hour, uint8_t min, bool synced);

#ifdef __cplusplus
}
#endif

#endif /* PAGE_STANDBY_H */
