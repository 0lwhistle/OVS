/**
 * @file st7789.h
 * @brief ST7789 TFT显示屏模块接口
 * 
 * 提供ST7789显示屏的初始化、绘图、刷新等功能。
 * 使用SPI总线通信，通过event_bus发布显示状态事件。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#ifndef ST7789_H
#define ST7789_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    ST7789_OK = 0,
    ST7789_ERR_NOT_INIT = -1,
    ST7789_ERR_PARAM = -2,
    ST7789_ERR_SPI = -3,
    ST7789_ERR_HW = -4,
} st7789_err_t;

/* ========== 类型定义 ========== */

/**
 * @brief 颜色结构 (RGB565)
 */
typedef struct {
    uint16_t value;  /**< RGB565颜色值 */
} st7789_color_t;

/**
 * @brief 矩形区域
 */
typedef struct {
    uint16_t x;      /**< X坐标 */
    uint16_t y;      /**< Y坐标 */
    uint16_t width;  /**< 宽度 */
    uint16_t height; /**< 高度 */
} st7789_rect_t;

/* ========== 预定义颜色宏 ========== */
#define ST7789_COLOR(r, g, b) ((st7789_color_t){ \
    ((uint16_t)(((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))})

#define ST7789_BLACK       ST7789_COLOR(0, 0, 0)
#define ST7789_WHITE       ST7789_COLOR(255, 255, 255)
#define ST7789_RED         ST7789_COLOR(255, 0, 0)
#define ST7789_GREEN       ST7789_COLOR(0, 255, 0)
#define ST7789_BLUE        ST7789_COLOR(0, 0, 255)
#define ST7789_YELLOW      ST7789_COLOR(255, 255, 0)
#define ST7789_CYAN        ST7789_COLOR(0, 255, 255)
#define ST7789_MAGENTA     ST7789_COLOR(255, 0, 255)

/* ========== 公共 API ========== */

/**
 * @brief 初始化ST7789显示屏
 * 
 * 初始化SPI总线和ST7789显示控制器。
 * 
 * @return ST7789_OK 成功
 * @return ST7789_ERR_SPI SPI初始化失败
 */
st7789_err_t st7789_init(void);

/**
 * @brief 反初始化ST7789显示屏
 * 
 * @return ST7789_OK 成功
 */
st7789_err_t st7789_deinit(void);

/**
 * @brief 获取屏幕宽度
 * 
 * @return 屏幕宽度（像素）
 */
uint16_t st7789_get_width(void);

/**
 * @brief 获取屏幕高度
 * 
 * @return 屏幕高度（像素）
 */
uint16_t st7789_get_height(void);

/**
 * @brief 设置背光状态
 * 
 * @param on true打开，false关闭
 * @return ST7789_OK 成功
 */
st7789_err_t st7789_set_backlight(bool on);

/**
 * @brief 清屏
 * 
 * @param color 填充颜色
 * @return ST7789_OK 成功
 */
st7789_err_t st7789_clear(st7789_color_t color);

/**
 * @brief 填充矩形区域
 * 
 * @param rect 矩形区域
 * @param color 填充颜色
 * @return ST7789_OK 成功
 */
st7789_err_t st7789_fill_rect(const st7789_rect_t* rect, st7789_color_t color);

/**
 * @brief 绘制像素点
 * 
 * @param x X坐标
 * @param y Y坐标
 * @param color 像素颜色
 * @return ST7789_OK 成功
 */
st7789_err_t st7789_draw_pixel(uint16_t x, uint16_t y, st7789_color_t color);

/**
 * @brief 块传输：设置窗口后把 RGB565 像素数据写入 GRAM（LVGL flush 用）
 *
 * @param x0,y0,x1,y1 窗口（闭区间，物理坐标）
 * @param data        像素数据（大端 RGB565 字节序，即 MSB 先发）
 * @param len         字节数，应等于窗口像素数×2
 */
st7789_err_t st7789_blit(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                         const void* data, size_t len);

/**
 * @brief 绘制线段
 * 
 * @param x0 起点X
 * @param y0 起点Y
 * @param x1 终点X
 * @param y1 终点Y
 * @param color 线段颜色
 * @return ST7789_OK 成功
 */
st7789_err_t st7789_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, st7789_color_t color);

/**
 * @brief 绘制矩形边框
 * 
 * @param rect 矩形区域
 * @param color 边框颜色
 * @return ST7789_OK 成功
 */
st7789_err_t st7789_draw_rect(const st7789_rect_t* rect, st7789_color_t color);

/**
 * @brief 绘制字符串
 * 
 * @param x 起始X坐标
 * @param y 起始Y坐标
 * @param str 字符串
 * @param color 文字颜色
 * @param bg_color 背景颜色
 * @param font_size 字体大小
 * @return ST7789_OK 成功
 */
st7789_err_t st7789_draw_string(uint16_t x, uint16_t y, const char* str, 
                                st7789_color_t color, st7789_color_t bg_color, uint8_t font_size);

/**
 * @brief 刷新显示缓冲区到屏幕
 * 
 * @return ST7789_OK 成功
 */
st7789_err_t st7789_flush(void);

/**
 * @brief 检查ST7789是否已初始化
 * 
 * @return true 已初始化
 * @return false 未初始化
 */
bool st7789_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* ST7789_H */
