/**
 * @file lvgl_app.h
 * @brief LVGL 应用层入口（ESP 端运行时）
 *
 * 初始化 LVGL（库 → 显示 port → 导航器 → 页面）并创建 lvgl_task。
 * PC 模拟器不使用本文件，见 sim/main.c（相同的 nav/page 复用）。
 */

#ifndef LVGL_APP_H
#define LVGL_APP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    LVGL_APP_OK = 0,
    LVGL_APP_ERR_INIT = -1,
} lvgl_app_err_t;

/* ========== 公共 API ========== */

/**
 * @brief 初始化 LVGL 应用并启动 lvgl_task（Core1 / 16KB 栈 / 优先级 4）
 *
 * 所有 LVGL 对象操作必须在 lvgl_task 上下文内，或经
 * lvgl_app_lock()/lvgl_app_unlock() 保护（LVGL 非线程安全）。
 */
lvgl_app_err_t lvgl_app_init(void);

/**
 * @brief 锁定 LVGL 互斥锁（其他任务访问 UI 前必须调用）
 */
void lvgl_app_lock(void);

/**
 * @brief 解锁 LVGL 互斥锁
 */
void lvgl_app_unlock(void);

/**
 * @brief 设置屏幕亮度（0-255；骨架阶段仅记录，背光 PWM 由 power_srv 接管）
 */
void lvgl_app_set_brightness(uint8_t brightness);

/**
 * @brief 获取屏幕亮度
 */
uint8_t lvgl_app_get_brightness(void);

#ifdef __cplusplus
}
#endif

#endif /* LVGL_APP_H */
