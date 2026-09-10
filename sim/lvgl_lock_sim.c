/**
 * @file lvgl_lock_sim.c
 * @brief PC 模拟器 lvgl_app 锁桩（单线程 lv_timer_handler，无需加锁）
 *
 * ESP 端实现在 src/lvgl/lvgl_app.c；presenters 经 lvgl_app_lock/unlock
 * 保护事件线程更新 UI，模拟器内为空实现保持同一套 presenter 代码。
 */

#include "lvgl_app.h"

void lvgl_app_lock(void) {
}

void lvgl_app_unlock(void) {
}
