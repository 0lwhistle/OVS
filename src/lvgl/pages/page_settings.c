/**
 * @file page_settings.c
 * @brief 页面层：设置页实现
 *
 * 行布局：返回钮 + 标题 → 音量（slider 实调 audio_player）→ 语言
 * （dropdown 即时切换）→ 设备名（占位）→ 待机（占位）。
 */

#include "page_settings.h"
#include "bridge.h"
#include "theme_base.h"
#include "ui_list_item.h"
#include "ui_round_btn.h"

static ui_list_item_t s_vol_item;
static ui_list_item_t s_lang_item;
static ui_list_item_t s_name_item;
static ui_list_item_t s_standby_item;

static lv_obj_t* s_vol_slider = NULL;
static lv_obj_t* s_vol_value = NULL;
static lv_obj_t* s_lang_dd = NULL;
static lv_obj_t* s_name_value = NULL;
static lv_obj_t* s_standby_value = NULL;

static page_settings_ops_t s_ops;

/* ==================== 事件转发（页面→presenter） ==================== */

static void vol_slider_cb(lv_event_t* e) {
    lv_obj_t* slider = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(slider);
    if (s_ops.on_volume) {
        s_ops.on_volume((uint8_t)v, s_ops.user_data);
    }
    if (s_vol_value) {
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d", (int)v);
        lv_label_set_text(s_vol_value, buf);
    }
}

static void lang_dd_cb(lv_event_t* e) {
    lv_obj_t* dd = lv_event_get_target(e);
    char buf[16];
    lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
    if (s_ops.on_language) {
        s_ops.on_language(buf, s_ops.user_data);
    }
}

static void back_clicked_cb(lv_event_t* e) {
    (void)e;
    if (s_ops.on_back) {
        s_ops.on_back(s_ops.user_data);
    }
}

/* ==================== 构建 ==================== */

lv_obj_t* page_settings_create(lv_obj_t* parent) {
    lv_obj_t* root = theme_page_root_create(parent);

    /* 头行：返回 + 标题 */
    lv_obj_t* head = lv_obj_create(root);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, lv_pct(100), 36);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 8, 0);

    ui_round_btn_t back_btn;
    ui_round_btn_create(&back_btn, head, LV_SYMBOL_LEFT, 30);
    lv_obj_add_event_cb(back_btn.root, back_clicked_cb, LV_EVENT_CLICKED, NULL);
    theme_label_create(head, _("SET_TITLE"), OVS_FONT_UI, OVS_COLOR_TEXT_MAIN);

    /* 行容器 */
    lv_obj_t* list = lv_obj_create(root);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 8, 0);

    /* 音量行 */
    ui_list_item_create(&s_vol_item, list, _("SET_VOLUME"));
    s_vol_slider = lv_slider_create(s_vol_item.slot);
    lv_obj_set_width(s_vol_slider, 110);
    lv_slider_set_range(s_vol_slider, 0, 100);
    lv_obj_set_style_bg_color(s_vol_slider, OVS_COLOR_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_vol_slider, OVS_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_vol_slider, OVS_COLOR_PRIMARY, LV_PART_KNOB);
    lv_obj_add_event_cb(s_vol_slider, vol_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    s_vol_value = theme_label_create(s_vol_item.slot, "--", OVS_FONT_UI, OVS_COLOR_TEXT_SECOND);

    /* 语言行 */
    ui_list_item_create(&s_lang_item, list, _("SET_LANGUAGE"));
    s_lang_dd = lv_dropdown_create(s_lang_item.slot);
    lv_dropdown_set_options(s_lang_dd, "zh-CN\nen-US");
    lv_obj_set_width(s_lang_dd, 110);
    lv_obj_set_style_text_font(s_lang_dd, OVS_FONT_SMALL, 0);
    lv_obj_add_event_cb(s_lang_dd, lang_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 设备名行（占位） */
    ui_list_item_create(&s_name_item, list, _("SET_DEVICE_NAME"));
    s_name_value = theme_label_create(s_name_item.slot, "--", OVS_FONT_UI,
                                      OVS_COLOR_TEXT_SECOND);

    /* 待机行（占位） */
    ui_list_item_create(&s_standby_item, list, _("SET_STANDBY"));
    s_standby_value = theme_label_create(s_standby_item.slot, "60s", OVS_FONT_UI,
                                         OVS_COLOR_TEXT_PLACEHOLDER);

    return root;
}

/* ==================== presenter 注入接口 ==================== */

void page_settings_set_ops(const page_settings_ops_t* ops) {
    if (ops) {
        s_ops = *ops;
    } else {
        lv_memset(&s_ops, 0, sizeof(s_ops));
    }
}

void page_settings_set_volume(uint8_t volume) {
    if (s_vol_slider) {
        lv_slider_set_value(s_vol_slider, volume, LV_ANIM_OFF);
    }
    if (s_vol_value) {
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%u", (unsigned)volume);
        lv_label_set_text(s_vol_value, buf);
    }
}

void page_settings_set_device_name(const char* name) {
    if (s_name_value && name) {
        lv_label_set_text(s_name_value, name);
    }
}
