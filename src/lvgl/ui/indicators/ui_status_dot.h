/**
 * @file ui_status_dot.h
 * @brief 指示层：状态圆点（在线/离线/自定义色）
 */

#ifndef UI_STATUS_DOT_H
#define UI_STATUS_DOT_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 创建状态圆点（size px 正圆） */
lv_obj_t* ui_status_dot_create(lv_obj_t* parent, lv_coord_t size);

/** 设置状态色（如 OVS_COLOR_SUCCESS/OVS_COLOR_DANGER） */
void ui_status_dot_set(lv_obj_t* dot, lv_color_t color);

#ifdef __cplusplus
}
#endif

#endif /* UI_STATUS_DOT_H */
