/**
 * @file ui_status_dot.c
 * @brief 指示层：状态圆点实现
 */

#include "ui_status_dot.h"
#include "theme_base.h"

lv_obj_t* ui_status_dot_create(lv_obj_t* parent, lv_coord_t size) {
    lv_obj_t* dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, size, size);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, OVS_COLOR_TEXT_PLACEHOLDER, 0);
    return dot;
}

void ui_status_dot_set(lv_obj_t* dot, lv_color_t color) {
    if (dot) {
        lv_obj_set_style_bg_color(dot, color, 0);
    }
}
