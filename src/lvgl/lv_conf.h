/**
 * @file lv_conf.h
 * @brief LVGL v9.5.0 双平台配置（ESP32-S3 与 PC 模拟器共用一份）
 *
 * 原则：只覆盖与上游 lv_conf_template.h 默认值不同的项，
 *       未列出的项由 lv_conf_internal.h 自动回填模板默认值。
 * 平台差异：仅 OVS_SIMULATOR（PC 模拟器构建，sim/CMakeLists.txt 注入宏）
 *       启用 SDL 后端；ESP 端显示由 src/lvgl/port/display_port.c 接入。
 *
 * 注：v8 的 LV_COLOR_16_SWAP 在 v9 已移除——将来 st7789 真屏对接时
 *       在 flush_cb 里调用 lv_draw_sw_rgb565_swap()，因此本配置两端通用。
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* ==================== 颜色 ==================== */
/* RGB565，与 ST7789 及模拟器一致（模板默认即 16，显式声明防漂移） */
#define LV_COLOR_DEPTH              16

/* ==================== 内存 ==================== */
/* libc malloc：ESP 走内部堆（大缓冲后续自行放 PSRAM），
 * 不用 LVGL 内置内存池（默认 64KB 静态数组太占内部 RAM） */
#define LV_USE_STDLIB_MALLOC        LV_STDLIB_CLIB

/* ==================== 刷新与 OS ==================== */
/* 30fps：SPI 屏 40MHz 局部刷新上限约 37fps；PC 模拟器无压力 */
#define LV_DEF_REFR_PERIOD          33
/* LVGL 内核不加锁：ESP 端由 lvgl_app 的 lvgl_task 单线程持有 + 互斥锁保护 */
#define LV_USE_OS                   LV_OS_NONE

/* ==================== 日志 ==================== */
#define LV_USE_LOG                  1
#define LV_LOG_LEVEL                LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF               1   /* printf 直出（ESP=串口控制台 / PC=stdout） */

/* ==================== 字体 ==================== */
/* 默认字体 montserrat_14（模板默认）；
 * 中文 CJK：LVGL 9.5 内置思源黑体 14/16（常用简体字集），作为 UI 基础字体 */
#define LV_FONT_MONTSERRAT_16       1
#define LV_FONT_MONTSERRAT_20       1
#define LV_FONT_MONTSERRAT_28       1
#define LV_FONT_MONTSERRAT_48       1
#define LV_FONT_SOURCE_HAN_SANS_SC_14_CJK 1
#define LV_FONT_SOURCE_HAN_SANS_SC_16_CJK 1

/* ==================== 平台差异 ==================== */
#if defined(OVS_SIMULATOR)
/* PC 模拟器：SDL2 窗口 + 鼠标/键盘输入（关窗即退出，LV_SDL_DIRECT_EXIT=1） */
#define LV_USE_SDL                  1
#endif

#endif /* LV_CONF_H */
