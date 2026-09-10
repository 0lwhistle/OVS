/**
 * @file page_home.c
 * @brief 页面层：主页实现（320x240 横屏蓝白主题）
 *
 * 布局：顶栏（标题+网络状态点+未读角标+设置钮）→ 大字时钟（未同步灰显）
 * → 温湿度/网络两张信息卡。页面禁止轮询阻塞：时间刷新由 presenter 的
 * 1s LVGL timer 驱动，其余数据事件驱动经 setter 注入。
 */

#include "page_home.h"
#include "bridge.h"
#include "theme_base.h"
#include "ui_card.h"
#include "ui_round_btn.h"
#include "ui_status_dot.h"

/* ---- 页面内控件句柄（单例页面，静态存储） ---- */
static lv_obj_t* s_time_label = NULL;
static lv_obj_t* s_time_hint = NULL;
static lv_obj_t* s_net_dot = NULL;
static lv_obj_t* s_unread_badge = NULL;
static ui_card_t s_temp_card;
static ui_card_t s_net_card;

/* presenter 注入的设置页跳转回调（每次 on_enter 重注入，reload 安全） */
static void (*s_on_settings_cb)(lv_event_t* e) = NULL;

static void settings_clicked_cb(lv_event_t* e) {
    if (s_on_settings_cb) {
        s_on_settings_cb(e);
    }
}

void page_home_set_nav_cb(void (*on_settings)(lv_event_t* e)) {
    s_on_settings_cb = on_settings;
}

/* ==================== 内部构建 ==================== */

static void top_bar_create(lv_obj_t* parent) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), 36);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 6, 0);

    theme_label_create(bar, "OVS", OVS_FONT_UI, OVS_COLOR_PRIMARY);
    theme_label_create(bar, _("APP_TITLE"), OVS_FONT_SMALL, OVS_COLOR_TEXT_SECOND);

    s_net_dot = ui_status_dot_create(bar, 10);

    /* 右侧：未读角标 + 设置按钮 */
    lv_obj_t* spacer = lv_obj_create(bar);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_flex_grow(spacer, 1);

    lv_obj_t* bell_wrap = lv_obj_create(bar);
    lv_obj_remove_style_all(bell_wrap);
    lv_obj_set_size(bell_wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_t* bell = theme_label_create(bell_wrap, LV_SYMBOL_BELL, OVS_FONT_UI,
                                        OVS_COLOR_TEXT_SECOND);
    lv_obj_set_pos(bell, 0, 0);
    s_unread_badge = lv_label_create(bell_wrap);
    lv_label_set_text(s_unread_badge, "0");
    lv_obj_set_style_bg_color(s_unread_badge, OVS_COLOR_DANGER, 0);
    lv_obj_set_style_bg_opa(s_unread_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_unread_badge, 8, 0);
    lv_obj_set_style_pad_hor(s_unread_badge, 4, 0);
    lv_obj_set_style_text_color(s_unread_badge, OVS_COLOR_CARD_BG, 0);
    lv_obj_set_style_text_font(s_unread_badge, OVS_FONT_SMALL, 0);
    lv_obj_set_pos(s_unread_badge, 12, -6);

    ui_round_btn_t settings_btn;
    ui_round_btn_create(&settings_btn, bar, LV_SYMBOL_SETTINGS, 30);
    lv_obj_add_event_cb(settings_btn.root, settings_clicked_cb, LV_EVENT_CLICKED, NULL);
}

static void clock_block_create(lv_obj_t* parent) {
    s_time_label = theme_label_create(parent, "--:--", OVS_FONT_NUM_XL,
                                      OVS_COLOR_TEXT_PLACEHOLDER);
    lv_obj_align(s_time_label, LV_ALIGN_TOP_MID, 0, 52);

    s_time_hint = theme_label_create(parent, _("HOME_TIME"), OVS_FONT_SMALL,
                                     OVS_COLOR_TEXT_PLACEHOLDER);
    lv_obj_align_to(s_time_hint, s_time_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
}

static void cards_create(lv_obj_t* parent) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 92);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ui_card_create(&s_temp_card, row, LV_SYMBOL_TINT, _("HOME_TEMP"));
    lv_obj_set_width(s_temp_card.root, 142);
    lv_obj_set_height(s_temp_card.root, lv_pct(100));
    ui_card_set_value(&s_temp_card, "--", "°C");

    ui_card_create(&s_net_card, row, LV_SYMBOL_WIFI, _("HOME_NET"));
    lv_obj_set_width(s_net_card.root, 142);
    lv_obj_set_height(s_net_card.root, lv_pct(100));
    ui_card_set_value(&s_net_card, "OFF", "");
}

/* ==================== 公共 API ==================== */

lv_obj_t* page_home_create(lv_obj_t* parent) {
    lv_obj_t* root = theme_page_root_create(parent);

    top_bar_create(root);
    clock_block_create(root);
    cards_create(root);

    /* 初始占位数据 */
    page_home_set_unread(0);
    return root;
}

void page_home_set_time(uint8_t hour, uint8_t min, bool synced) {
    if (!s_time_label) {
        return;
    }
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)hour, (unsigned)min);
    lv_label_set_text(s_time_label, buf);
    /* synced=false：照常显示并灰显 */
    lv_obj_set_style_text_color(s_time_label, synced ? OVS_COLOR_TEXT_MAIN
                                                     : OVS_COLOR_TEXT_PLACEHOLDER, 0);
    lv_obj_set_style_text_color(s_time_hint, synced ? OVS_COLOR_TEXT_SECOND
                                                    : OVS_COLOR_TEXT_PLACEHOLDER, 0);
    lv_label_set_text(s_time_hint, _("HOME_TIME"));
}

void page_home_set_sensor(bool valid, int32_t temp_m_c, int32_t humi_m_p) {
    if (valid) {
        char val[16];
        lv_snprintf(val, sizeof(val), "%d.%d", (int)(temp_m_c / 1000),
                    (int)((temp_m_c < 0 ? -temp_m_c : temp_m_c) % 1000) / 100);
        ui_card_set_value(&s_temp_card, val, "°C");
        char sub[24];
        lv_snprintf(sub, sizeof(sub), "%s %d.%d%%",
                    _("HOME_HUMI"), (int)(humi_m_p / 1000),
                    (int)(humi_m_p % 1000) / 100);
        ui_card_set_sub(&s_temp_card, sub);
        ui_card_set_dimmed(&s_temp_card, false);
    } else {
        ui_card_set_value(&s_temp_card, "--", "°C");
        ui_card_set_sub(&s_temp_card, _("SENSOR_NO_DATA"));
        ui_card_set_dimmed(&s_temp_card, true);
    }
}

void page_home_set_net(int mode, const char* ssid, const char* ip,
                       int8_t rssi, bool switching) {
    const char* mode_str = (mode == 1) ? "STA" : (mode == 2) ? "AP" : "OFF";
    ui_card_set_value(&s_net_card, mode_str, "");
    if (switching) {
        ui_card_set_sub(&s_net_card, "...");
        ui_status_dot_set(s_net_dot, OVS_COLOR_TEXT_PLACEHOLDER);
        return;
    }
    char sub[64];
    if (mode == 0) {
        lv_snprintf(sub, sizeof(sub), "%s", _("HOME_NET_OFF"));
        ui_status_dot_set(s_net_dot, OVS_COLOR_DANGER);
    } else {
        lv_snprintf(sub, sizeof(sub), "%s\n%s  %d dBm", ssid ? ssid : "", ip ? ip : "",
                    (int)rssi);
        ui_status_dot_set(s_net_dot, OVS_COLOR_SUCCESS);
    }
    ui_card_set_sub(&s_net_card, sub);
}

void page_home_set_unread(int count) {
    if (s_unread_badge) {
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d", count);
        lv_label_set_text(s_unread_badge, buf);
    }
}
