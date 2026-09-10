/**
 * @file theme_base.c
 * @brief 蓝白主题实现：共享样式集中管理
 */

#include "theme_base.h"

static lv_style_t s_style_page;
static lv_style_t s_style_card;
static bool s_inited = false;

void theme_base_init(void) {
    if (s_inited) {
        return;
    }

    /* 页面容器：主题底色 */
    lv_style_init(&s_style_page);
    lv_style_set_bg_color(&s_style_page, OVS_COLOR_PAGE_BG);
    lv_style_set_bg_opa(&s_style_page, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_page, 0);
    lv_style_set_radius(&s_style_page, 0);
    lv_style_set_pad_all(&s_style_page, 0);

    /* 白卡片：圆角 + 细描边 + 轻阴影 */
    lv_style_init(&s_style_card);
    lv_style_set_bg_color(&s_style_card, OVS_COLOR_CARD_BG);
    lv_style_set_bg_opa(&s_style_card, LV_OPA_COVER);
    lv_style_set_radius(&s_style_card, 12);
    lv_style_set_border_width(&s_style_card, 1);
    lv_style_set_border_color(&s_style_card, OVS_COLOR_LINE);
    lv_style_set_shadow_width(&s_style_card, 12);
    lv_style_set_shadow_color(&s_style_card, lv_color_hex(0x1E6FFF));
    lv_style_set_shadow_opa(&s_style_card, LV_OPA_10);
    lv_style_set_shadow_offset_y(&s_style_card, 2);
    lv_style_set_pad_all(&s_style_card, 8);

    s_inited = true;
}

lv_style_t* theme_style_page(void) {
    return &s_style_page;
}

lv_style_t* theme_style_card(void) {
    return &s_style_card;
}

lv_obj_t* theme_page_root_create(lv_obj_t* parent) {
    theme_base_init();
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_add_style(root, theme_style_page(), 0);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(root, 8, 0);
    return root;
}

lv_obj_t* theme_label_create(lv_obj_t* parent, const char* text,
                             const lv_font_t* font, lv_color_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}
