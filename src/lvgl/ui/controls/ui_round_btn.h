/**
 * @file ui_round_btn.h
 * @brief 控件层：圆形按钮（点击 + 按住语义预留）
 */

#ifndef UI_ROUND_BTN_H
#define UI_ROUND_BTN_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t* root;   /**< 按钮对象（lv_button） */
} ui_round_btn_t;

/**
 * @brief 创建圆形按钮（主蓝底 + 白色符号）
 * @param symbol LVGL 内置符号，如 LV_SYMBOL_SETTINGS
 * @param size 直径（px）
 */
void ui_round_btn_create(ui_round_btn_t* out, lv_obj_t* parent,
                         const char* symbol, lv_coord_t size);

/**
 * @brief 设置回调（均可为 NULL）
 * @param on_click 短按（LV_EVENT_CLICKED）
 * @param on_hold  按住（LV_EVENT_LONG_PRESSED，录音/对讲语义预留）
 */
void ui_round_btn_set_cb(ui_round_btn_t* btn,
                         void (*on_click)(lv_event_t* e),
                         void (*on_hold)(lv_event_t* e),
                         void* user_data);

#ifdef __cplusplus
}
#endif

#endif /* UI_ROUND_BTN_H */
