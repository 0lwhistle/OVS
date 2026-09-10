/**
 * @file ui_card.h
 * @brief 控件层：信息卡片（图标 + 主值 + 副值）
 *
 * 纯 UI 实现，无业务依赖；调用方持有 ui_card_t（建议静态存储）。
 */

#ifndef UI_CARD_H
#define UI_CARD_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t* root;    /**< 卡片容器（白卡片样式） */
    lv_obj_t* icon;    /**< 图标/符号标签 */
    lv_obj_t* title;   /**< 标题（次级色小字） */
    lv_obj_t* value;   /**< 主值（大字） */
    lv_obj_t* unit;    /**< 主值单位（跟随主值右侧） */
    lv_obj_t* sub;     /**< 副值（次级色） */
} ui_card_t;

/**
 * @brief 创建卡片
 * @param icon_symbol LVGL 内置符号（如 LV_SYMBOL_HOME）或 NULL 不显示
 * @param title 标题文案（已翻译字符串）
 */
void ui_card_create(ui_card_t* out, lv_obj_t* parent,
                    const char* icon_symbol, const char* title);

/** 设置主值（数值文本，如 "26.5"）；unit 可为 NULL（保持不变） */
void ui_card_set_value(ui_card_t* card, const char* value, const char* unit);

/** 设置副值（如 "湿度 48%"） */
void ui_card_set_sub(ui_card_t* card, const char* sub);

/** 主值灰显（数据无效/未同步） */
void ui_card_set_dimmed(ui_card_t* card, bool dimmed);

#ifdef __cplusplus
}
#endif

#endif /* UI_CARD_H */
