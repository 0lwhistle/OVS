/**
 * @file ui_bootstrap.h
 * @brief UI 引导：主题 → 桥接 → 导航器 → 各 presenter → 首页
 */

#ifndef UI_BOOTSTRAP_H
#define UI_BOOTSTRAP_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 装配并启动整个 UI（lvgl_app_init / sim main 调用，幂等）
 * @param root 页面挂载父对象（通常 lv_screen_active()）
 */
void ui_bootstrap_run(lv_obj_t* root);

#ifdef __cplusplus
}
#endif

#endif /* UI_BOOTSTRAP_H */
