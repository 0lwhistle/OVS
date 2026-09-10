/**
 * @file ui_round_btn.c
 * @brief 控件层：圆形按钮实现
 */

#include "ui_round_btn.h"
#include "theme_base.h"

void ui_round_btn_create(ui_round_btn_t* out, lv_obj_t* parent,
                         const char* symbol, lv_coord_t size) {
    lv_memset(out, 0, sizeof(*out));

    out->root = lv_button_create(parent);
    lv_obj_set_size(out->root, size, size);
    lv_obj_set_style_radius(out->root, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(out->root, OVS_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_color(out->root, OVS_COLOR_PRIMARY_DARK, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(out->root, 14, 0);
    lv_obj_set_style_shadow_opa(out->root, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(out->root, OVS_COLOR_PRIMARY, 0);

    lv_obj_t* icon = lv_label_create(out->root);
    lv_label_set_text(icon, symbol ? symbol : "");
    lv_obj_set_style_text_font(icon, OVS_FONT_UI, 0);
    lv_obj_set_style_text_color(icon, OVS_COLOR_CARD_BG, 0);
    lv_obj_center(icon);
}

void ui_round_btn_set_cb(ui_round_btn_t* btn,
                         void (*on_click)(lv_event_t* e),
                         void (*on_hold)(lv_event_t* e),
                         void* user_data) {
    if (!btn || !btn->root) {
        return;
    }
    if (on_click) {
        lv_obj_add_event_cb(btn->root, on_click, LV_EVENT_CLICKED, user_data);
    }
    if (on_hold) {
        /* 按住语义预留：对讲/录音按钮未来接入（事件已通） */
        lv_obj_add_event_cb(btn->root, on_hold, LV_EVENT_LONG_PRESSED, user_data);
    }
}
