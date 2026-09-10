/**
 * @file ui_card.c
 * @brief 控件层：信息卡片实现
 */

#include "ui_card.h"
#include "theme_base.h"

void ui_card_create(ui_card_t* out, lv_obj_t* parent,
                    const char* icon_symbol, const char* title) {
    lv_memset(out, 0, sizeof(*out));

    out->root = lv_obj_create(parent);
    lv_obj_remove_style_all(out->root);
    lv_obj_add_style(out->root, theme_style_card(), 0);
    lv_obj_clear_flag(out->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(out->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(out->root, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(out->root, 2, 0);

    /* 头行：图标 + 标题 */
    lv_obj_t* head = lv_obj_create(out->root);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 4, 0);

    if (icon_symbol) {
        out->icon = theme_label_create(head, icon_symbol, OVS_FONT_UI, OVS_COLOR_PRIMARY);
    }
    out->title = theme_label_create(head, title, OVS_FONT_SMALL, OVS_COLOR_TEXT_SECOND);

    /* 主值行：大字 + 单位 */
    lv_obj_t* val_row = lv_obj_create(out->root);
    lv_obj_remove_style_all(val_row);
    lv_obj_set_size(val_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(val_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(val_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(val_row, 2, 0);

    out->value = theme_label_create(val_row, "--", OVS_FONT_NUM_L, OVS_COLOR_TEXT_MAIN);
    out->unit = theme_label_create(val_row, "", OVS_FONT_UI, OVS_COLOR_TEXT_SECOND);
    out->sub = theme_label_create(out->root, "", OVS_FONT_SMALL, OVS_COLOR_TEXT_SECOND);
}

void ui_card_set_value(ui_card_t* card, const char* value, const char* unit) {
    if (!card || !card->value) {
        return;
    }
    lv_label_set_text(card->value, value ? value : "--");
    if (unit) {
        lv_label_set_text(card->unit, unit);
    }
}

void ui_card_set_sub(ui_card_t* card, const char* sub) {
    if (card && card->sub) {
        lv_label_set_text(card->sub, sub ? sub : "");
    }
}

void ui_card_set_dimmed(ui_card_t* card, bool dimmed) {
    if (!card) {
        return;
    }
    lv_obj_set_style_text_color(card->value,
                                dimmed ? OVS_COLOR_TEXT_PLACEHOLDER : OVS_COLOR_TEXT_MAIN, 0);
}
