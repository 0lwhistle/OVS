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
    (void)area;
    (void)px_map;
    /* TODO(6.2): lv_draw_sw_rgb565_swap(px_map, px_num) → st7789_set_window + DMA blit */
    s_flush_count++;
    lv_display_flush_ready(disp);
}

lv_display_t* display_port_init(void) {
    if (s_disp) {
        return s_disp;
    }

    /* 尺寸读设备树 lcd_display 节点（compatible 定位），失败用默认值 */
    int32_t w = OVS_DISPLAY_DEFAULT_W;
    int32_t h = OVS_DISPLAY_DEFAULT_H;
    dtree_node_t* dev = dtree_find_by_compatible("st7789-lcd");
    if (dev) {
        int32_t v = 0;
        if (dtree_get_int(dev, "width", &v) == DTREE_OK && v > 0) {
            w = v;
        }
        if (dtree_get_int(dev, "height", &v) == DTREE_OK && v > 0) {
            h = v;
        }
    } else {
        LOGW(TAG, "lcd_display node not found, using %dx%d",
             OVS_DISPLAY_DEFAULT_W, OVS_DISPLAY_DEFAULT_H);
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
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2, buf_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    LOGI(TAG, "display %dx%d, draw buf %uB x2 (null sink)",
         (int)w, (int)h, (unsigned)buf_size);
    return s_disp;
}

uint32_t display_port_get_flush_count(void) {
    return s_flush_count;
}
