/**
 * @file display_port.h
 * @brief 显示移植层（ESP 端）：LVGL display ↔ 硬件屏幕的边界
 *
 * 骨架阶段：创建 LVGL display（320×240，尺寸读设备树）+ null 输出 flush，
 * 仅让渲染管线在真机上跑通；真实 st7789 对接见 REFACTORING_PLAN 6.2。
 */

#ifndef DISPLAY_PORT_H
#define DISPLAY_PORT_H

#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化显示移植层（幂等）
 * @return LVGL display 句柄；失败返回 NULL
 */
lv_display_t* display_port_init(void);

/**
 * @brief 获取累计 flush 次数（冒烟监控用）
 */
uint32_t display_port_get_flush_count(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_PORT_H */
