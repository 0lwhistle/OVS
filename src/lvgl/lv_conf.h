/**
 * @file lv_conf.h
 * @brief LVGL 配置文件
 * 
 * 针对 ESP32-S3 (16MB Flash, 8MB PSRAM) 优化
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ==================== 颜色配置 ==================== */
#define LV_COLOR_DEPTH          16      /* RGB565 */
#define LV_COLOR_16_SWAP        1       /* SPI 显示屏需要字节交换 */
#define LV_COLOR_SCREEN_TRANSP  0
#define LV_COLOR_MIX_ROUND_OFS  128
#define LV_COLOR_CHROMA_KEY     lv_color_hex(0x00ff00)

/* ==================== 内存配置 ==================== */
#define LV_MEM_CUSTOM           1       /* 使用自定义内存分配 */
#define LV_MEM_CUSTOM_INCLUDE   <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC     malloc
#define LV_MEM_CUSTOM_FREE      free
#define LV_MEM_CUSTOM_REALLOC   realloc

/* ==================== 显示配置 ==================== */
#define LV_DISP_DEF_REFR_PERIOD 16      /* 60fps */
#define LV_DISP_ROT_MAX_BUF     (10 * 1024)  /* 旋转缓冲区大小 */

/* ==================== 图形配置 ==================== */
#define LV_DRAW_COMPLEX         1       /* 启用复杂绘制 */
#define LV_SHADOW_CACHE_SIZE    0
#define LV_IMG_CACHE_DEF_SIZE   0

/* ==================== 日志配置 ==================== */
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1       /* 使用 printf 输出日志 */

/* ==================== 断言配置 ==================== */
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/* ==================== 字体配置 ==================== */
#define LV_FONT_MONTSERRAT_8    0
#define LV_FONT_MONTSERRAT_10   0
#define LV_FONT_MONTSERRAT_12   1       /* 常用小字体 */
#define LV_FONT_MONTSERRAT_14   1       /* 常用正文字体 */
#define LV_FONT_MONTSERRAT_16   1       /* 常用标题字体 */
#define LV_FONT_MONTSERRAT_18   0
#define LV_FONT_MONTSERRAT_20   0
#define LV_FONT_MONTSERRAT_22   0
#define LV_FONT_MONTSERRAT_24   0
#define LV_FONT_MONTSERRAT_26   0
#define LV_FONT_MONTSERRAT_28   0
#define LV_FONT_MONTSERRAT_30   0
#define LV_FONT_MONTSERRAT_32   0
#define LV_FONT_MONTSERRAT_34   0
#define LV_FONT_MONTSERRAT_36   0
#define LV_FONT_MONTSERRAT_38   0
#define LV_FONT_MONTSERRAT_40   0
#define LV_FONT_MONTSERRAT_42   0
#define LV_FONT_MONTSERRAT_44   0
#define LV_FONT_MONTSERRAT_46   0
#define LV_FONT_MONTSERRAT_48   0

#define LV_FONT_DEFAULT         &lv_font_montserrat_14

#define LV_FONT_FMT_TXT_LARGE   0
#define LV_USE_FONT_COMPRESSED   0
#define LV_USE_FONT_SUBPX       0
#define LV_USE_FONT_PLACEHOLDER 1       /* 字体缺失时显示占位符 */

/* ==================== 文本配置 ==================== */
#define LV_TXT_ENC              LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS     " ,.;:-_"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_COLOR_CMD       "#"

/* ==================== 控件配置 ==================== */
#define LV_USE_ARC          1
#define LV_USE_BAR          1
#define LV_USE_BTN          1
#define LV_USE_BTNMATRIX    1
#define LV_USE_CANVAS       0
#define LV_USE_CHECKBOX     1
#define LV_USE_DROPDOWN     1
#define LV_USE_IMG          1
#define LV_USE_LABEL        1
#define LV_USE_LINE         1
#define LV_USE_ROLLER       1
#define LV_USE_SLIDER       1
#define LV_USE_SWITCH       1
#define LV_USE_TEXTAREA     1
#define LV_USE_TABLE        1

/* ==================== 高级控件 ==================== */
#define LV_USE_ANIMIMG      0
#define LV_USE_CALENDAR     0
#define LV_USE_CHART        1
#define LV_USE_COLORWHEEL   0
#define LV_USE_IMGBTN       1
#define LV_USE_KEYBOARD     1
#define LV_USE_LED          1
#define LV_USE_LIST         1
#define LV_USE_MENU         1
#define LV_USE_METER        1
#define LV_USE_MSGBOX       1
#define LV_USE_SPAN         1
#define LV_USE_SPINBOX      0
#define LV_USE_SPINNER      1
#define LV_USE_TABVIEW      1
#define LV_USE_TILEVIEW     1
#define LV_USE_WIN          1

/* ==================== 主题配置 ==================== */
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1       /* 使用暗色主题 */
#define LV_USE_THEME_BASIC      1

/* ==================== 布局配置 ==================== */
#define LV_USE_FLEX     1
#define LV_USE_GRID     1

/* ==================== 性能配置 ==================== */
#define LV_USE_PERF_MONITOR     0       /* 关闭性能监控 */
#define LV_USE_MEM_MONITOR      0       /* 关闭内存监控 */
#define LV_USE_REFR_DEBUG       0

/* ==================== 其他功能 ==================== */
#define LV_USE_SNAPSHOT         0
#define LV_USE_MONKEY           0
#define LV_USE_GRIDNAV          0
#define LV_USE_FRAGMENT         0
#define LV_USE_IMGFONT          0
#define LV_USE_MSG              1       /* 启用消息机制 */
#define LV_USE_IME_PINYIN       0

/* ==================== 中文支持 ==================== */
/* 如需中文显示，在 fonts/ 目录添加中文字体 */

#endif /* LV_CONF_H */
