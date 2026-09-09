/**
 * @file nav.h
 * @brief 导航层：页面注册表 + 滑动导航（骨架版）
 *
 * 按 REFACTORING_PLAN 6.4 规划三横页 tileview 主框架：
 *   [intercom] [home] [clock]
 * 后续扩展 nav_push/nav_pop 页面栈与页面生命周期回调（on_hide 停自身 timer）。
 */

#ifndef NAV_H
#define NAV_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NAV_PAGE_INTERCOM = 0,
    NAV_PAGE_HOME,
    NAV_PAGE_CLOCK,
    NAV_PAGE_COUNT,
} nav_page_t;

/**
 * @brief 初始化导航器并构建默认页面（幂等）
 * @param parent 挂载父对象（通常为 lv_screen_active()）
 */
void nav_init(lv_obj_t* parent);

/**
 * @brief 跳转到指定页（滑动动画）
 */
void nav_goto(nav_page_t page);

#ifdef __cplusplus
}
#endif

#endif /* NAV_H */
