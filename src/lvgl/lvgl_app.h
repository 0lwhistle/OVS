/**
 * @file lvgl_app.h
 * @brief LVGL 应用层入口
 * 
 * 初始化 LVGL，协调六层架构工作。
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
    LVGL_APP_ERR_PARAM = -2,
} lvgl_app_err_t;

/* ========== 公共 API ========== */

/**
 * @brief 初始化 LVGL 应用
 * 
 * 按顺序初始化: LVGL库 → 显示驱动 → 输入设备 → 导航器 → 页面
 */
lvgl_app_err_t lvgl_app_init(void);

/**
 * @brief LVGL 主循环处理
 * @param timeout_ms 超时时间
 */
void lvgl_app_handler(uint32_t timeout_ms);

/**
 * @brief 设置屏幕亮度
 * @param brightness 亮度 (0-255)
 */
void lvgl_app_set_brightness(uint8_t brightness);

/**
 * @brief 获取屏幕亮度
 */
uint8_t lvgl_app_get_brightness(void);

/**
 * @brief 锁定 LVGL 互斥锁 (多任务访问时使用)
 */
void lvgl_app_lock(void);

/**
 * @brief 解锁 LVGL 互斥锁
 */
void lvgl_app_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* LVGL_APP_H */
