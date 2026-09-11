/**
 * @file display_port.c
 * @brief 显示移植层实现（ESP 端，骨架：null 输出）
 *
 * 屏幕 320×240（横屏，设备树 lcd_display 为准，读不到时用默认值）。
 * 双 20 行局部刷新缓冲放内部 DMA RAM（2 × 12.8KB）。
 * flush_cb 仅计数不发送——真实 st7789 set_window + DMA blit 见 6.2。
 */

#include "display_port.h"
#include "lvgl.h"
#include "dtree.h"
#include "logger.h"
#include "mem.h"

#include "st7789.h"
#include "esp_heap_caps.h"
#include <stdlib.h>

static const char* TAG = "[DISP_PORT]";

#define OVS_DISPLAY_DEFAULT_W   320
#define OVS_DISPLAY_DEFAULT_H   240
#define OVS_DRAW_BUF_LINES      20

static lv_display_t* s_disp = NULL;
static uint32_t s_flush_count = 0;
static void* s_buf1 = NULL;
static void* s_buf2 = NULL;

static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;

    /* LVGL v9 输出小端 RGB565，ST7789 要 MSB 先发：整块字节交换 */
    lv_draw_sw_rgb565_swap(px_map, (size_t)w * h);

    st7789_err_t err = st7789_blit((uint16_t)area->x1, (uint16_t)area->y1,
                                   (uint16_t)area->x2, (uint16_t)area->y2,
                                   px_map, (size_t)w * h * 2);
    if (err != ST7789_OK) {
        LOGW(TAG, "blit failed: %d (%dx%d @%d,%d)", err, (int)w, (int)h,
             (int)area->x1, (int)area->y1);
    }
    s_flush_count++;
    lv_display_flush_ready(disp);
}

lv_display_t* display_port_init(void) {
    if (s_disp) {
        return s_disp;
    }

    /* 尺寸以 st7789 驱动实际初始化的为准（其读设备树 + 旋转逻辑） */
    int32_t w = st7789_get_width();
    int32_t h = st7789_get_height();
    if (w <= 0 || h <= 0) {
        w = OVS_DISPLAY_DEFAULT_W;
        h = OVS_DISPLAY_DEFAULT_H;
        LOGW(TAG, "st7789 not ready, fallback %dx%d", (int)w, (int)h);
    }

    size_t buf_size = (size_t)w * OVS_DRAW_BUF_LINES * 2;   /* RGB565 */
    s_buf1 = mem_heap_alloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    s_buf2 = mem_heap_alloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!s_buf1 || !s_buf2) {
        LOGE(TAG, "Draw buffer alloc failed (%uB x2)", (unsigned)buf_size);
        mem_free(s_buf1);
        mem_free(s_buf2);
        s_buf1 = s_buf2 = NULL;
        return NULL;
    }

    s_disp = lv_display_create(w, h);
    if (!s_disp) {
        LOGE(TAG, "lv_display_create failed");
        return NULL;
    }
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2, buf_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    LOGI(TAG, "display %dx%d, draw buf %uB x2 (st7789 blit)",
         (int)w, (int)h, (unsigned)buf_size);
    return s_disp;
}

uint32_t display_port_get_flush_count(void) {
    return s_flush_count;
}
