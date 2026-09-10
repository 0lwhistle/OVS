/**
 * @file theme_base.h
 * @brief 蓝白主题：色彩/字体/样式常量集中定义
 *
 * 主蓝 #1E6FFF 系、白卡片、#F5F7FA 页面底、深色文本三级（主/次/占位）。
 * 与 WEB 端（Vue）保持同套色值，代码零共享。
 * 本头属于 themes 层（层内头），ui/widgets/pages 可安全 include。
 */

#ifndef THEME_BASE_H
#define THEME_BASE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 色彩常量（蓝白主题） ========== */
#define OVS_COLOR_PRIMARY           lv_color_hex(0x1E6FFF)   /**< 主蓝 */
#define OVS_COLOR_PRIMARY_DARK      lv_color_hex(0x0F4FD8)   /**< 主蓝-按下 */
#define OVS_COLOR_PRIMARY_LIGHT     lv_color_hex(0xEAF1FF)   /**< 主蓝-浅底 */
#define OVS_COLOR_PAGE_BG           lv_color_hex(0xF5F7FA)   /**< 页面底 */
#define OVS_COLOR_CARD_BG           lv_color_hex(0xFFFFFF)   /**< 白卡片 */
#define OVS_COLOR_TEXT_MAIN         lv_color_hex(0x1F2937)   /**< 文本-主 */
#define OVS_COLOR_TEXT_SECOND       lv_color_hex(0x5B6472)   /**< 文本-次 */
#define OVS_COLOR_TEXT_PLACEHOLDER  lv_color_hex(0x9CA3AF)   /**< 文本-占位/灰显 */
#define OVS_COLOR_SUCCESS           lv_color_hex(0x22C55E)   /**< 成功/在线 */
#define OVS_COLOR_DANGER            lv_color_hex(0xEF4444)   /**< 错误/离线 */
#define OVS_COLOR_LINE              lv_color_hex(0xE2E8F0)   /**< 分隔线/描边 */
#define OVS_COLOR_STANDBY_BG        lv_color_hex(0x0B1220)   /**< 待机底 */

/* ========== 字体（LVGL 9.5 内置） ========== */
/* 中文基础字体（思源黑体常用简体字集） */
#define OVS_FONT_UI     (&lv_font_source_han_sans_sc_16_cjk)
#define OVS_FONT_SMALL  (&lv_font_source_han_sans_sc_14_cjk)
/* 数字大字（时钟/主值） */
#define OVS_FONT_NUM_L  (&lv_font_montserrat_28)
#define OVS_FONT_NUM_XL (&lv_font_montserrat_48)

/**
 * @brief 初始化共享样式（幂等；在创建任何页面前调用一次）
 */
void theme_base_init(void);

/**
 * @brief 共享样式取用（theme_base_init 之后有效）
 */
lv_style_t* theme_style_page(void);   /**< 页面容器：#F5F7FA 底、无滚动条 */
lv_style_t* theme_style_card(void);   /**< 白卡片：圆角12、细阴影、内边距 */

/**
 * @brief 页面根容器便捷创建：铺主题底色、禁滚动、全尺寸
 */
lv_obj_t* theme_page_root_create(lv_obj_t* parent);

/**
 * @brief 文本标签便捷创建（默认字体/颜色三级）
 */
lv_obj_t* theme_label_create(lv_obj_t* parent, const char* text,
                             const lv_font_t* font, lv_color_t color);

#ifdef __cplusplus
}
#endif

#endif /* THEME_BASE_H */
