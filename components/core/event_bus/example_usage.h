/**
 * @file example_usage.h
 * @brief 事件总线使用示例头文件
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef EXAMPLE_USAGE_H
#define EXAMPLE_USAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi模块: 发布WiFi连接事件
 * 
 * @param ssid WiFi名称
 * @param rssi 信号强度
 */
void example_wifi_on_connected(const char* ssid, int rssi);

/**
 * @brief 传感器模块: 发布温湿度事件
 * 
 * @param temp 温度 (°C)
 * @param humidity 湿度 (%RH)
 */
void example_sensor_update(float temp, float humidity);

/**
 * @brief Web模块初始化
 */
void example_web_module_init(void);

/**
 * @brief Web模块反初始化
 */
void example_web_module_deinit(void);

/**
 * @brief UI模块初始化
 */
void example_ui_module_init(void);

/**
 * @brief UI模块反初始化
 */
void example_ui_module_deinit(void);

/**
 * @brief 完整使用示例
 * 
 * 演示事件总线的完整使用流程
 */
void example_full_usage(void);

#ifdef __cplusplus
}
#endif

#endif /* EXAMPLE_USAGE_H */
