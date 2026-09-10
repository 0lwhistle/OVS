/**
 * @file ui_list_item.c
 * @brief 控件层：设置列表项实现
 */

#include "ui_list_item.h"
#include "theme_base.h"

void ui_list_item_create(ui_list_item_t* out, lv_obj_t* parent, const char* title) {
    lv_memset(out, 0, sizeof(*out));

    out->root = lv_obj_create(parent);
    lv_obj_remove_style_all(out->root);
    lv_obj_add_style(out->root, theme_style_card(), 0);
    lv_obj_clear_flag(out->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(out->root, lv_pct(100), 44);

    out->title = theme_label_create(out->root, title, OVS_FONT_UI, OVS_COLOR_TEXT_MAIN);
    lv_obj_align(out->title, LV_ALIGN_LEFT_MID, 0, 0);

    out->slot = lv_obj_create(out->root);
    lv_obj_remove_style_all(out->slot);
    lv_obj_set_size(out->slot, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(out->slot, LV_ALIGN_RIGHT_MID, 0, 0);
}

void ui_list_item_set_value(ui_list_item_t* item, lv_obj_t* value_label, const char* text) {
    if (value_label) {
        lv_label_set_text(value_label, text ? text : "");
        lv_obj_align_to(value_label, item->root, LV_ALIGN_RIGHT_MID, -4, 0);
    }
}
