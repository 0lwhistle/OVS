/**
 * @file ui_list_item.h
 * @brief 控件层：设置列表项（标题 + 右侧值/控件插槽）
 */

#ifndef UI_LIST_ITEM_H
#define UI_LIST_ITEM_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t* root;     /**< 列表项容器（白卡片） */
    lv_obj_t* title;    /**< 左侧标题 */
    lv_obj_t* slot;     /**< 右侧内容插槽（slider/dropdown/label 由页面放） */
} ui_list_item_t;

/**
 * @brief 创建列表项（高度固定行高，标题左、插槽右）
 */
void ui_list_item_create(ui_list_item_t* out, lv_obj_t* parent, const char* title);

/** 设置右侧为纯文本值（不使用 slot 控件时） */
void ui_list_item_set_value(ui_list_item_t* item, lv_obj_t* value_label, const char* text);

#ifdef __cplusplus
}
#endif

#endif /* UI_LIST_ITEM_H */
