/**
 * @file ui_value_label.h
 * @brief 指示层：值标签（数值 + 单位，基线对齐）
 */

#ifndef UI_VALUE_LABEL_H
#define UI_VALUE_LABEL_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t* root;   /**< 横排容器 */
    lv_obj_t* num;    /**< 数值标签 */
    lv_obj_t* unit;   /**< 单位标签（较小、次级色） */
} ui_value_label_t;

/**
 * @brief 创建值标签
 * @param num_font 数值字体（如 OVS_FONT_NUM_L）
 * @param unit 初始单位文案（可为 ""）
 */
void ui_value_label_create(ui_value_label_t* out, lv_obj_t* parent,
                           const lv_font_t* num_font, const char* unit);

/** 设置数值文本（单位保持不变） */
void ui_value_label_set(ui_value_label_t* vl, const char* value);

/** 数值灰显切换 */
void ui_value_label_set_dimmed(ui_value_label_t* vl, bool dimmed);

#ifdef __cplusplus
}
#endif

#endif /* UI_VALUE_LABEL_H */
