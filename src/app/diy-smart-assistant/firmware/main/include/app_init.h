/**
 * 应用初始化头文件
 */

#ifndef APP_INIT_H
#define APP_INIT_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化所有硬件
 */
esp_err_t app_init_hardware(void);

/**
 * 获取电池电压
 * @return 电压值 (V)
 */
float get_battery_voltage(void);

/**
 * 检查系统状态
 */
void check_system_status(void);

#ifdef __cplusplus
}
#endif

#endif // APP_INIT_H
