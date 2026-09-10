/**
 * @file page_standby.c
 * @brief 页面层：待机占位页实现
 *
 * 深色底 + 大字时钟 + 唤醒提示。覆盖层由 navigator（I6）创建/销毁，
 * 本页只在传入的 parent 上构建内容；时钟用页内 1s LVGL timer 自刷
 * （timer 挂在页面根对象上，随对象销毁自动清理）。
 */

#include "page_standby.h"
#include "bridge.h"
#include "theme_base.h"

static lv_obj_t* s_clock = NULL;
static lv_obj_t* s_hint = NULL;

/** LV_EVENT_DELETE 时清理 timer（timer 的 user_data 指向 root） */
static void timer_autodel_cb(lv_event_t* e) {
    lv_timer_t* timer = (lv_timer_t*)lv_event_get_user_data(e);
    if (timer) {
        lv_timer_del(timer);
    }
}

static void standby_time_timer_cb(lv_timer_t* timer) {
    lv_obj_t* root = (lv_obj_t*)lv_timer_get_user_data(timer);
    if (!s_clock || !lv_obj_is_valid(root)) {
        return;
    }
    ui_bridge_time_t t;
    ui_bridge_time_get(&t);
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)t.hour, (unsigned)t.min);
    lv_label_set_text(s_clock, buf);
}

lv_obj_t* page_standby_screen_create(lv_obj_t* parent) {
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(root, OVS_COLOR_STANDBY_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    s_clock = theme_label_create(root, "--:--", OVS_FONT_NUM_XL,
                                 lv_color_hex(0x3A4A6B));
    lv_obj_align(s_clock, LV_ALIGN_CENTER, 0, -14);

    theme_label_create(root, "OVS", OVS_FONT_SMALL, lv_color_hex(0x26324A));

    s_hint = theme_label_create(root, _("STANDBY_HINT"), OVS_FONT_SMALL,
                                lv_color_hex(0x33415C));
    lv_obj_align(s_hint, LV_ALIGN_CENTER, 0, 40);

    /* 页内 1s 时钟 timer：root 删除时经 DELETE 事件回收 */
    lv_timer_t* timer = lv_timer_create(standby_time_timer_cb, 1000, root);
    lv_obj_add_event_cb(root, timer_autodel_cb, LV_EVENT_DELETE, timer);
    return root;
}

void page_standby_set_time(uint8_t hour, uint8_t min, bool synced) {
    (void)synced;
    if (s_clock) {
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)hour, (unsigned)min);
        lv_label_set_text(s_clock, buf);
    }
}
