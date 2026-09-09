/**
 * @file page_home.c
 * @brief 主页实现（骨架）
 *
 * 布局按 320×240 横屏设计，尺寸用百分比/内容自适应以便两端通用。
 * 时钟数据只经 bridge_time 获取（UI 不直接碰后端）。
 */

#include "page_home.h"
#include "bridge_time.h"
#include "lvgl.h"

static lv_obj_t* s_clock_label = NULL;      /* 大字时钟 HH:MM */
static lv_obj_t* s_status_time_label = NULL; /* 状态栏 HH:MM:SS */

static void clock_timer_cb(lv_timer_t* timer) {
    (void)timer;
    if (!s_clock_label || !s_status_time_label) {
        return;
    }

    uint8_t h = 0, m = 0, s = 0;
    bridge_time_get(&h, &m, &s);

    char buf[16];
    lv_snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)h, (unsigned)m);
    lv_label_set_text(s_clock_label, buf);
    lv_snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                (unsigned)h, (unsigned)m, (unsigned)s);
    lv_label_set_text(s_status_time_label, buf);
}

/* 状态栏：左侧标题，右侧实时时间 */
static void status_bar_create(lv_obj_t* parent) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, lv_pct(100), 28);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(bar, 8, 0);

    lv_obj_t* title = lv_label_create(bar);
    lv_label_set_text(title, "OVS");

    s_status_time_label = lv_label_create(bar);
    lv_label_set_text(s_status_time_label, "--:--:--");
}

void page_home_create(lv_obj_t* parent) {
    status_bar_create(parent);

    /* 大字时钟 */
    s_clock_label = lv_label_create(parent);
    lv_obj_set_style_text_font(s_clock_label, &lv_font_montserrat_48, 0);
    lv_label_set_text(s_clock_label, "--:--");
    lv_obj_align(s_clock_label, LV_ALIGN_CENTER, 0, -10);

    /* 占位卡片 */
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, 180, 56);
    lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* card_label = lv_label_create(card);
    lv_label_set_text_fmt(card_label, "OVS  LVGL v%d.%d.%d",
                          LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
                          LVGL_VERSION_PATCH);
    lv_obj_center(card_label);

    /* 立即渲染一次，之后每秒刷新 */
    clock_timer_cb(NULL);
    lv_timer_create(clock_timer_cb, 1000, NULL);
}
