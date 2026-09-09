/**
 * @file page_home.h
 * @brief 主页：状态栏 + 大字时钟 + 占位卡片（骨架版）
 */

#ifndef PAGE_HOME_H
#define PAGE_HOME_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建主页（由 navigator 挂载到其 tile 上）
 * @param parent 页面父容器
 */
void page_home_create(lv_obj_t* parent);

#ifdef __cplusplus
}
#endif

#endif /* PAGE_HOME_H */
