/**
 * @file cst816s.h
 * @brief CST816S 电容触控屏模块
 * 
 * 提供CST816S触控屏的初始化、触控事件读取功能。
 * 使用I2C总线通信，通过event_bus发布触控事件。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef CST816S_H
#define CST816S_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    CST816S_OK = 0,
    CST816S_ERR_NOT_INIT = -1,
    CST816S_ERR_PARAM = -2,
    CST816S_ERR_I2C = -3,
    CST816S_ERR_NO_TOUCH = -4,
} cst816s_err_t;

/* ========== 类型定义 ========== */

/**
 * @brief 触控手势类型
 */
typedef enum {
    CST816S_GESTURE_NONE = 0,       /**< 无手势 */
    CST816S_GESTURE_SLIDE_UP = 1,   /**< 上滑 */
    CST816S_GESTURE_SLIDE_DOWN = 2, /**< 下滑 */
    CST816S_GESTURE_SLIDE_LEFT = 3, /**< 左滑 */
    CST816S_GESTURE_SLIDE_RIGHT = 4,/**< 右滑 */
    CST816S_GESTURE_SINGLE_TAP = 5, /**< 单击 */
    CST816S_GESTURE_DOUBLE_TAP = 6, /**< 双击 */
    CST816S_GESTURE_LONG_PRESS = 7, /**< 长按 */
} cst816s_gesture_t;

/**
 * @brief 触控数据结构
 */
typedef struct {
    uint16_t x;                     /**< X坐标 */
    uint16_t y;                     /**< Y坐标 */
    cst816s_gesture_t gesture;      /**< 手势类型 */
    uint8_t finger_num;             /**< 触摸手指数量 */
    uint32_t timestamp;             /**< 时间戳 (ms) */
} cst816s_touch_t;

/**
 * @brief 触控回调函数类型
 * 
 * @param touch 触控数据
 * @param user_data 用户数据
 */
typedef void (*cst816s_touch_cb_t)(const cst816s_touch_t* touch, void* user_data);

/* ========== 公共 API ========== */

/**
 * @brief 初始化CST816S触控屏
 * 
 * 初始化I2C总线和CST816S触控屏硬件。
 * 
 * @return CST816S_OK 成功
 * @return CST816S_ERR_I2C I2C初始化失败
 */
cst816s_err_t cst816s_init(void);

/**
 * @brief 反初始化CST816S触控屏
 * 
 * @return CST816S_OK 成功
 */
cst816s_err_t cst816s_deinit(void);

/**
 * @brief 读取触控数据
 * 
 * @param touch 输出参数，存储触控数据
 * @return CST816S_OK 成功
 * @return CST816S_ERR_NOT_INIT 未初始化
 * @return CST816S_ERR_NO_TOUCH 无触摸
 * @return CST816S_ERR_I2C I2C读取失败
 */
cst816s_err_t cst816s_read(cst816s_touch_t* touch);

/**
 * @brief 检查是否有触摸
 * 
 * @param touched 输出参数，存储触摸状态
 * @return CST816S_OK 成功
 * @return CST816S_ERR_NOT_INIT 未初始化
 */
cst816s_err_t cst816s_is_touched(bool* touched);

/**
 * @brief 获取触摸坐标
 * 
 * @param x 输出参数，存储X坐标
 * @param y 输出参数，存储Y坐标
 * @return CST816S_OK 成功
 * @return CST816S_ERR_NOT_INIT 未初始化
 * @return CST816S_ERR_NO_TOUCH 无触摸
 */
cst816s_err_t cst816s_get_position(uint16_t* x, uint16_t* y);

/**
 * @brief 获取手势类型
 * 
 * @param gesture 输出参数，存储手势类型
 * @return CST816S_OK 成功
 * @return CST816S_ERR_NOT_INIT 未初始化
 */
cst816s_err_t cst816s_get_gesture(cst816s_gesture_t* gesture);

/**
 * @brief 检查CST816S是否已初始化
 * 
 * @return true 已初始化
 * @return false 未初始化
 */
bool cst816s_is_initialized(void);

/**
 * @brief 启动周期性触控检测任务
 * 
 * 使用tasker注册周期任务，定期检测触控并通过event_bus发布。
 * 
 * @param interval_ms 检测间隔 (ms)
 * @return CST816S_OK 成功
 * @return CST816S_ERR_NOT_INIT 未初始化
 */
cst816s_err_t cst816s_start_periodic_read(uint32_t interval_ms);

/**
 * @brief 停止周期性触控检测任务
 * 
 * @return CST816S_OK 成功
 */
cst816s_err_t cst816s_stop_periodic_read(void);

#ifdef __cplusplus
}
#endif

#endif /* CST816S_H */
