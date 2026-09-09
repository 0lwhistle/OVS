/**
 * @file st7789.c
 * @brief ST7789 TFT显示屏模块实现
 * 
 * 实现ST7789显示屏的初始化、绘图、刷新等功能。
 * 使用SPI总线通信，通过event_bus发布显示状态事件。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "mem.h"
#include "st7789.h"
#include "spi_drv.h"
#include "event_bus.h"
#include "logger.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static const char* TAG = "[ST7789]";

/* 本模块服务的设备（设备树 compatible），初始化时按它查找自己的节点 */
#define ST7789_DT_COMPAT   "st7789-lcd"

/* ========================================================================== */
/*                              常量定义                                       */
/* ========================================================================== */

/** ST7789 命令 */
#define ST7789_CMD_NOP          0x00
#define ST7789_CMD_SWRESET      0x01
#define ST7789_CMD_SLPOUT       0x11
#define ST7789_CMD_NORON        0x13
#define ST7789_CMD_INVON        0x21
#define ST7789_CMD_DISPON       0x29
#define ST7789_CMD_CASET        0x2A
#define ST7789_CMD_RASET        0x2B
#define ST7789_CMD_RAMWR        0x2C
#define ST7789_CMD_MADCTL       0x36
#define ST7789_CMD_COLMOD       0x3A

/** MADCTL 位定义 */
#define ST7789_MADCTL_MY        0x80
#define ST7789_MADCTL_MX        0x40
#define ST7789_MADCTL_MV        0x20
#define ST7789_MADCTL_ML        0x10
#define ST7789_MADCTL_RGB       0x00
#define ST7789_MADCTL_BGR       0x08

/* ========================================================================== */
/*                              内部变量                                       */
/* ========================================================================== */

/** SPI驱动句柄 */
static spi_drv_handle_t s_spi_handle = NULL;

/** SPI设备句柄 */
static spi_device_handle_t s_spi_dev = NULL;

/** DC引脚 */
static int s_dc_pin = -1;

/** RST引脚 */
static int s_rst_pin = -1;

/** BL引脚 */
static int s_bl_pin = -1;

/** 屏幕宽度 */
static uint16_t s_width = 240;

/** 屏幕高度 */
static uint16_t s_height = 280;

/** 初始化标志 */
static bool s_initialized = false;

/** 显示缓冲区（可选，用于双缓冲） */
static uint16_t* s_framebuffer = NULL;

/** 刷屏分片行数：分片间总线空闲，共享总线上的其他设备（如 W25Q128）可插空 */
#define ST7789_FLUSH_SLICE_LINES   24

/** 分片暂存缓冲（内部 DMA RAM，常驻），替代逐帧大块 DMA 安全拷贝 */
static uint8_t* s_slice_buf = NULL;
#define ST7789_SLICE_BYTES  ((size_t)s_width * ST7789_FLUSH_SLICE_LINES * 2)

/* ========================================================================== */
/*                              内部函数                                       */
/* ========================================================================== */

/**
 * @brief 发送命令
 */
static void st7789_write_cmd(uint8_t cmd) {
    gpio_set_level(s_dc_pin, 0);
    spi_drv_write(s_spi_handle, s_spi_dev, &cmd, 1);
}

/**
 * @brief 发送数据
 */
static void st7789_write_data(const void* data, size_t len) {
    gpio_set_level(s_dc_pin, 1);
    spi_drv_write(s_spi_handle, s_spi_dev, data, len);
}

/**
 * @brief 发送单字节数据
 */
static void st7789_write_data_byte(uint8_t data) {
    st7789_write_data(&data, 1);
}

/**
 * @brief 硬件复位
 */
static void st7789_hw_reset(void) {
    if (s_rst_pin < 0) return;
    
    gpio_set_level(s_rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(s_rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

/**
 * @brief 设置显示窗口
 */
static void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t data[4];
    
    st7789_write_cmd(ST7789_CMD_CASET);
    data[0] = (x0 >> 8) & 0xFF;
    data[1] = x0 & 0xFF;
    data[2] = (x1 >> 8) & 0xFF;
    data[3] = x1 & 0xFF;
    st7789_write_data(data, 4);
    
    st7789_write_cmd(ST7789_CMD_RASET);
    data[0] = (y0 >> 8) & 0xFF;
    data[1] = y0 & 0xFF;
    data[2] = (y1 >> 8) & 0xFF;
    data[3] = y1 & 0xFF;
    st7789_write_data(data, 4);
}

/**
 * @brief 初始化ST7789控制器
 */
static st7789_err_t st7789_hw_init(void) {
    /* 硬件复位 */
    st7789_hw_reset();
    
    /* 软件复位 */
    st7789_write_cmd(ST7789_CMD_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));
    
    /* 退出睡眠模式 */
    st7789_write_cmd(ST7789_CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    /* 设置颜色模式为RGB565 */
    st7789_write_cmd(ST7789_CMD_COLMOD);
    st7789_write_data_byte(0x55);
    
    /* 设置显示方向 */
    st7789_write_cmd(ST7789_CMD_MADCTL);
    st7789_write_data_byte(ST7789_MADCTL_RGB);
    
    /* 打开显示反转（ST7789通常需要） */
    st7789_write_cmd(ST7789_CMD_INVON);
    
    /* 正常显示模式 */
    st7789_write_cmd(ST7789_CMD_NORON);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    /* 打开显示 */
    st7789_write_cmd(ST7789_CMD_DISPON);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    return ST7789_OK;
}

/**
 * @brief 初始化GPIO
 */
static st7789_err_t st7789_init_gpio(dtree_node_t* dev_node) {
    /* 从设备节点读取引脚配置 */
    dtree_err_t err;
    int32_t dc_pin, rst_pin, bl_pin;

    err = dtree_get_int(dev_node, "dc_pin", &dc_pin);
    DTREE_CHECK_ERROR("Read dc_pin", err); if (err != DTREE_OK) {
        return ST7789_ERR_SPI;
    }
    s_dc_pin = (int)dc_pin;

    err = dtree_get_int(dev_node, "rst_pin", &rst_pin);
    DTREE_CHECK_ERROR("Read rst_pin", err); if (err != DTREE_OK) {
        return ST7789_ERR_SPI;
    }
    s_rst_pin = (int)rst_pin;

    err = dtree_get_int(dev_node, "bl_pin", &bl_pin);
    DTREE_CHECK_ERROR("Read bl_pin", err); if (err != DTREE_OK) {
        return ST7789_ERR_SPI;
    }
    s_bl_pin = (int)bl_pin;
    
    /* 配置DC引脚为输出 */
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    
    io_conf.pin_bit_mask = (1ULL << s_dc_pin);
    gpio_config(&io_conf);
    
    /* 配置RST引脚为输出 */
    io_conf.pin_bit_mask = (1ULL << s_rst_pin);
    gpio_config(&io_conf);
    
    /* 配置BL引脚为输出 */
    io_conf.pin_bit_mask = (1ULL << s_bl_pin);
    gpio_config(&io_conf);
    
    /* 默认关闭背光 */
    gpio_set_level(s_bl_pin, 0);
    
    LOGI(TAG, "GPIO initialized: DC=%d, RST=%d, BL=%d", 
         s_dc_pin, s_rst_pin, s_bl_pin);
    
    return ST7789_OK;
}

/* ========================================================================== */
/*                              公共API实现                                    */
/* ========================================================================== */

st7789_err_t st7789_init(void) {
    if (s_initialized) {
        LOGW(TAG, "Already initialized");
        return ST7789_OK;
    }
    
    LOGI(TAG, "Initializing ST7789 display...");

    /* 按 compatible 定位自己的设备节点，父节点即所属 SPI 总线 */
    dtree_node_t* dev_node = dtree_find_by_compatible(ST7789_DT_COMPAT);
    if (!dev_node) {
        LOGE(TAG, "Device node '%s' not found in device tree", ST7789_DT_COMPAT);
        return ST7789_ERR_PARAM;
    }
    dtree_node_t* bus_node = dtree_get_parent(dev_node);
    if (!bus_node) {
        LOGE(TAG, "Device node '%s' has no parent bus node", ST7789_DT_COMPAT);
        return ST7789_ERR_PARAM;
    }

    /* 初始化GPIO */
    st7789_err_t err = st7789_init_gpio(dev_node);
    if (err != ST7789_OK) {
        LOGE(TAG, "GPIO init failed");
        return err;
    }

    /* 加载SPI总线配置（从父总线节点） */
    spi_drv_config_t spi_config;
    spi_drv_err_t spi_err = spi_drv_load_config(bus_node, &spi_config);
    if (spi_err != SPI_DRV_OK) {
        LOGE(TAG, "Failed to load SPI config: %d", spi_err);
        return ST7789_ERR_SPI;
    }

    /* 初始化SPI驱动 */
    spi_err = spi_drv_init(&spi_config, &s_spi_handle);
    if (spi_err != SPI_DRV_OK) {
        LOGE(TAG, "Failed to init SPI: %d", spi_err);
        return ST7789_ERR_SPI;
    }

    /* 添加LCD设备到SPI总线（设备属性从自己的节点读取） */
    int32_t spi_freq;
    dtree_get_int(dev_node, "spi_freq_mhz", &spi_freq);
    if (spi_freq == 0) spi_freq = 40;

    int32_t cs_pin;
    dtree_get_int(dev_node, "cs_pin", &cs_pin);

    /* 刷屏分片暂存缓冲：常驻内部 DMA RAM，一次分配整个运行期复用 */
    s_slice_buf = mem_dma_alloc(ST7789_SLICE_BYTES);
    if (!s_slice_buf) {
        LOGW(TAG, "Slice buffer alloc failed (%u bytes), flush falls back to whole-frame",
             (unsigned)ST7789_SLICE_BYTES);
    }

    spi_dev_config_t dev_config = {
        .cs_pin = (int)cs_pin,
        .clock_speed_hz = (int)(spi_freq * 1000000),
        .mode = 0,
        .xfer_mode = SPI_XFER_MODE_DMA_SYNC,  // LCD使用DMA，帧缓冲大
        .max_transfer_sz = (int)(s_slice_buf ? ST7789_SLICE_BYTES
                                             : (size_t)s_width * s_height * 2 + 1024),
    };
    spi_err = spi_drv_add_device(s_spi_handle, &dev_config, &s_spi_dev);
    if (spi_err != SPI_DRV_OK) {
        LOGE(TAG, "Failed to add SPI device: %d", spi_err);
        spi_drv_deinit(s_spi_handle);
        s_spi_handle = NULL;
        return ST7789_ERR_SPI;
    }
    
    /* 初始化ST7789控制器 */
    err = st7789_hw_init();
    if (err != ST7789_OK) {
        LOGE(TAG, "HW init failed");
        if (s_spi_dev) {
            spi_drv_remove_device(s_spi_handle, s_spi_dev);
            s_spi_dev = NULL;
        }
        spi_drv_deinit(s_spi_handle);
        s_spi_handle = NULL;
        return err;
    }
    
    /* 打开背光 */
    st7789_set_backlight(true);
    
    /* 分配帧缓冲 */
    s_framebuffer = (uint16_t*)mem_malloc(s_width * s_height * 2);
    if (!s_framebuffer) {
        LOGW(TAG, "Failed to allocate framebuffer, drawing directly to display");
    }
    
    s_initialized = true;
    
    LOGI(TAG, "ST7789 display initialized: %dx%d", s_width, s_height);
    
    /* 发布显示就绪事件 */
    EVENT_BUS_PUBLISH_EMPTY(EVENT_DISPLAY_READY);
    
    return ST7789_OK;
}

st7789_err_t st7789_deinit(void) {
    if (!s_initialized) {
        return ST7789_OK;
    }
    
    LOGI(TAG, "Deinitializing ST7789 display...");
    
    /* 关闭背光 */
    st7789_set_backlight(false);
    
    /* 释放帧缓冲 */
    if (s_framebuffer) {
        mem_free(s_framebuffer);
        s_framebuffer = NULL;
    }

    /* 释放刷屏分片暂存缓冲 */
    if (s_slice_buf) {
        mem_free(s_slice_buf);
        s_slice_buf = NULL;
    }
    
    /* 反初始化SPI */
    if (s_spi_handle) {
        if (s_spi_dev) {
            spi_drv_remove_device(s_spi_handle, s_spi_dev);
            s_spi_dev = NULL;
        }
        spi_drv_deinit(s_spi_handle);
        s_spi_handle = NULL;
    }
    
    s_initialized = false;
    
    LOGI(TAG, "ST7789 display deinitialized");
    
    return ST7789_OK;
}

uint16_t st7789_get_width(void) {
    return s_width;
}

uint16_t st7789_get_height(void) {
    return s_height;
}

st7789_err_t st7789_set_backlight(bool on) {
    if (s_bl_pin < 0) {
        return ST7789_ERR_NOT_INIT;
    }
    
    gpio_set_level(s_bl_pin, on ? 1 : 0);
    LOGI(TAG, "Backlight %s", on ? "ON" : "OFF");
    
    return ST7789_OK;
}

st7789_err_t st7789_clear(st7789_color_t color) {
    st7789_rect_t rect = {0, 0, s_width, s_height};
    return st7789_fill_rect(&rect, color);
}

st7789_err_t st7789_fill_rect(const st7789_rect_t* rect, st7789_color_t color) {
    if (!rect) {
        return ST7789_ERR_PARAM;
    }
    
    if (!s_initialized) {
        return ST7789_ERR_NOT_INIT;
    }
    
    /* 设置窗口 */
    st7789_set_window(rect->x, rect->y, 
                      rect->x + rect->width - 1, 
                      rect->y + rect->height - 1);
    
    /* 准备颜色数据 */
    uint16_t color_be = ((color.value & 0xFF00) >> 8) | ((color.value & 0x00FF) << 8);
    
    /* 发送颜色数据 */
    st7789_write_cmd(ST7789_CMD_RAMWR);
    
    /* 逐行填充（避免缓冲区过大） */
    uint16_t* line_buf = (uint16_t*)mem_malloc(rect->width * 2);
    if (!line_buf) {
        return ST7789_ERR_HW;
    }
    
    for (uint16_t i = 0; i < rect->width; i++) {
        line_buf[i] = color_be;
    }
    
    gpio_set_level(s_dc_pin, 1);
    for (uint16_t y = 0; y < rect->height; y++) {
        spi_drv_write(s_spi_handle, s_spi_dev, line_buf, rect->width * 2);
    }
    
    mem_free(line_buf);
    
    return ST7789_OK;
}

st7789_err_t st7789_draw_pixel(uint16_t x, uint16_t y, st7789_color_t color) {
    if (x >= s_width || y >= s_height) {
        return ST7789_ERR_PARAM;
    }
    
    st7789_rect_t rect = {x, y, 1, 1};
    return st7789_fill_rect(&rect, color);
}

st7789_err_t st7789_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, 
                              st7789_color_t color) {
    /* Bresenham线段算法 */
    int dx = abs((int)x1 - (int)x0);
    int dy = abs((int)y1 - (int)y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        st7789_draw_pixel(x0, y0, color);
        
        if (x0 == x1 && y0 == y1) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
    
    return ST7789_OK;
}

st7789_err_t st7789_draw_rect(const st7789_rect_t* rect, st7789_color_t color) {
    if (!rect) {
        return ST7789_ERR_PARAM;
    }
    
    st7789_draw_line(rect->x, rect->y, rect->x + rect->width - 1, rect->y, color);
    st7789_draw_line(rect->x, rect->y + rect->height - 1, rect->x + rect->width - 1, rect->y + rect->height - 1, color);
    st7789_draw_line(rect->x, rect->y, rect->x, rect->y + rect->height - 1, color);
    st7789_draw_line(rect->x + rect->width - 1, rect->y, rect->x + rect->width - 1, rect->y + rect->height - 1, color);
    
    return ST7789_OK;
}

st7789_err_t st7789_draw_string(uint16_t x, uint16_t y, const char* str, 
                                st7789_color_t color, st7789_color_t bg_color, uint8_t font_size) {
    /* 简化的字符绘制，实际应使用字体库 */
    if (!str || !s_initialized) {
        return ST7789_ERR_PARAM;
    }
    
    uint16_t cur_x = x;
    while (*str) {
        /* 简化：使用矩形代替实际字符 */
        st7789_rect_t char_rect = {cur_x, y, font_size, font_size * 2};
        st7789_fill_rect(&char_rect, (*str != ' ') ? color : bg_color);
        cur_x += font_size + 2;
        str++;
    }
    
    return ST7789_OK;
}

st7789_err_t st7789_flush(void) {
    if (!s_initialized || !s_framebuffer) {
        return ST7789_ERR_NOT_INIT;
    }

    /* 设置整个屏幕窗口 */
    st7789_set_window(0, 0, s_width - 1, s_height - 1);

    /* 发送帧缓冲数据 */
    st7789_write_cmd(ST7789_CMD_RAMWR);

    /* 需要字节交换（帧缓冲内为已交换的大端像素，直接发送） */
    gpio_set_level(s_dc_pin, 1);

    size_t frame_bytes = (size_t)s_width * s_height * 2;
    spi_drv_err_t spi_err;

    if (s_slice_buf) {
        /* 分片发送：片与片之间总线空闲，其他 SPI 设备事务可插空，
         * 共享总线时 flash 操作的尾延迟从整帧(约40ms)降为单片(约3ms) */
        size_t offset = 0;
        while (offset < frame_bytes) {
            size_t chunk = frame_bytes - offset;
            if (chunk > ST7789_SLICE_BYTES) {
                chunk = ST7789_SLICE_BYTES;
            }
            memcpy(s_slice_buf, (const uint8_t*)s_framebuffer + offset, chunk);
            spi_err = spi_drv_write(s_spi_handle, s_spi_dev, s_slice_buf, chunk);
            if (spi_err != SPI_DRV_OK) {
                LOGE(TAG, "Flush slice failed at %u/%u: %d",
                     (unsigned)offset, (unsigned)frame_bytes, spi_err);
                return ST7789_ERR_SPI;
            }
            offset += chunk;
        }
    } else {
        /* 暂存缓冲不可用：退回整帧发送（spi_drv 内部做 DMA 安全拷贝） */
        spi_err = spi_drv_write(s_spi_handle, s_spi_dev, s_framebuffer, frame_bytes);
        if (spi_err != SPI_DRV_OK) {
            return ST7789_ERR_SPI;
        }
    }

    return ST7789_OK;
}

bool st7789_is_initialized(void) {
    return s_initialized;
}
