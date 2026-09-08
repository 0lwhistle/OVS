/**
 * @file [module_name].h
 * @brief [模块功能描述]
 * @version 1.0
 * @date [日期]
 */

#ifndef [MODULE_NAME]_H
#define [MODULE_NAME]_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 模块状态
 */
typedef enum {
    [MODULE_NAME]_STATE_IDLE = 0,      // 空闲状态
    [MODULE_NAME]_STATE_RUNNING,       // 运行状态
    [MODULE_NAME]_STATE_ERROR,         // 错误状态
} [module_name]_state_t;

/**
 * @brief 模块配置
 */
typedef struct {
    // 配置参数
} [module_name]_config_t;

/**
 * @brief 模块状态信息
 */
typedef struct {
    [module_name]_state_t state;       // 当前状态
    // 其他状态信息
} [module_name]_status_t;

/**
 * @brief 回调函数类型
 */
typedef void (*[module_name]_callback_t)([module_name]_state_t state, void *data);

/**
 * @brief 初始化模块
 * 
 * @param config 模块配置
 * @return esp_err_t 
 */
esp_err_t [module_name]_init(const [module_name]_config_t *config);

/**
 * @brief 反初始化模块
 * 
 * @return esp_err_t 
 */
esp_err_t [module_name]_deinit(void);

/**
 * @brief 启动模块
 * 
 * @return esp_err_t 
 */
esp_err_t [module_name]_start(void);

/**
 * @brief 停止模块
 * 
 * @return esp_err_t 
 */
esp_err_t [module_name]_stop(void);

/**
 * @brief 获取模块状态
 * 
 * @param status 状态信息
 * @return esp_err_t 
 */
esp_err_t [module_name]_get_status([module_name]_status_t *status);

/**
 * @brief 设置回调函数
 * 
 * @param callback 回调函数
 * @return esp_err_t 
 */
esp_err_t [module_name]_set_callback([module_name]_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // [MODULE_NAME]_H
