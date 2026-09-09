/**
 * @file nav.c
 * @brief 导航层实现（骨架：tileview 三横页）
 */

#include "nav.h"
#include "logger.h"
#include "page_home.h"

static const char* TAG = "[NAV]";

static lv_obj_t* s_tileview = NULL;
static lv_obj_t* s_tiles[NAV_PAGE_COUNT] = {0};

/* 占位页：后续由真实页面替换 */
static void placeholder_create(lv_obj_t* tile, const char* text) {
    lv_obj_t* label = lv_label_create(tile);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(label);
}

void nav_init(lv_obj_t* parent) {
    if (s_tileview) {
        LOGW(TAG, "Already initialized");
        return;
    }

    s_tileview = lv_tileview_create(parent);
    lv_obj_set_size(s_tileview, lv_pct(100), lv_pct(100));

    s_tiles[NAV_PAGE_INTERCOM] = lv_tileview_add_tile(s_tileview, 0, 0, LV_DIR_HOR);
    s_tiles[NAV_PAGE_HOME]     = lv_tileview_add_tile(s_tileview, 1, 0, LV_DIR_HOR);
    s_tiles[NAV_PAGE_CLOCK]    = lv_tileview_add_tile(s_tileview, 2, 0, LV_DIR_HOR);

    page_home_create(s_tiles[NAV_PAGE_HOME]);
    placeholder_create(s_tiles[NAV_PAGE_INTERCOM], "Intercom\n(LORA TODO)");
    placeholder_create(s_tiles[NAV_PAGE_CLOCK], "Clock & Alarms\n(TODO)");

    /* 默认落在主页 */
    lv_obj_scroll_to_view(s_tiles[NAV_PAGE_HOME], LV_ANIM_OFF);

    LOGI(TAG, "navigator ready (3 tiles)");
}

void nav_goto(nav_page_t page) {
    if (page >= NAV_PAGE_COUNT || !s_tiles[page]) {
        LOGW(TAG, "nav_goto: invalid page %d", (int)page);
        return;
    }
    lv_obj_scroll_to_view(s_tiles[page], LV_ANIM_ON);
}
