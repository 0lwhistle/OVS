/**
 * @file ui_value_label.c
 * @brief 指示层：值标签实现
 */

#include "ui_value_label.h"
#include "theme_base.h"

void ui_value_label_create(ui_value_label_t* out, lv_obj_t* parent,
                           const lv_font_t* num_font, const char* unit) {
    lv_memset(out, 0, sizeof(*out));

    out->root = lv_obj_create(parent);
    lv_obj_remove_style_all(out->root);
    lv_obj_set_size(out->root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(out->root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(out->root, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(out->root, 2, 0);

    out->num = lv_label_create(out->root);
    lv_label_set_text(out->num, "--");
    lv_obj_set_style_text_font(out->num, num_font, 0);
    lv_obj_set_style_text_color(out->num, OVS_COLOR_TEXT_MAIN, 0);

    out->unit = lv_label_create(out->root);
    lv_label_set_text(out->unit, unit ? unit : "");
    lv_obj_set_style_text_font(out->unit, OVS_FONT_SMALL, 0);
    lv_obj_set_style_text_color(out->unit, OVS_COLOR_TEXT_SECOND, 0);
}

void ui_value_label_set(ui_value_label_t* vl, const char* value) {
    if (vl && vl->num) {
        lv_label_set_text(vl->num, value ? value : "--");
    }
}

void ui_value_label_set_dimmed(ui_value_label_t* vl, bool dimmed) {
    if (vl && vl->num) {
        lv_obj_set_style_text_color(vl->num,
                                    dimmed ? OVS_COLOR_TEXT_PLACEHOLDER : OVS_COLOR_TEXT_MAIN, 0);
    }
}
