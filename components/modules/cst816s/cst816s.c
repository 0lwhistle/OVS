/**
 * @file cst816s.c
 * @brief CST816S 电容触控屏模块实现
 * 
 * 实现CST816S触控屏的初始化、触控事件读取功能。
 * 使用I2C总线通信，通过event_bus发布触控事件。
 * 使用tasker进行周期性触控检测。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "cst816s.h"
#include "i2c_drv.h"
#include "event_bus.h"
#include "tasker.h"
#include "logger.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include <stdio.h>
#include <string.h>

static const char* TAG = "[CST816S]";

/* 本模块服务的设备（设备树 compatible），初始化时按它查找自己的节点 */
#define CST816S_DT_COMPAT   "cst816s-touch"

/* ========================================================================== */
/*                              常量定义                                       */
/* ========================================================================== */

/** CST816S I2C地址 */
#define CST816S_I2C_ADDR        0x15

/** CST816S 寄存器地址 */
#define CST816S_REG_GESTURE     0x01
#define CST816S_REG_FINGER_NUM  0x02
#define CST816S_REG_XPOS_H     0x03
#define CST816S_REG_XPOS_L     0x04
#define CST816S_REG_YPOS_H     0x05
#define CST816S_REG_YPOS_L     0x06
#define CST816S_REG_CHIP_ID    0xA7
#define CST816S_REG_PROJ_ID    0xA8
#define CST816S_REG_FW_VER     0xA9
#define CST816S_REG_SLEEP_MODE  0xFE

/** 默认检测间隔 (ms) */
#define CST816S_DEFAULT_INTERVAL_MS   20

/* ========================================================================== */
/*                              内部变量                                       */
/* ========================================================================== */

/** I2C驱动句柄 */
static i2c_drv_handle_t s_i2c_handle = NULL;

/** INT引脚 */
static int s_int_pin = -1;

/** RST引脚 */
static int s_rst_pin = -1;

/** 初始化标志 */
static bool s_initialized = false;

/** 最近一次触控数据 */
static cst816s_touch_t s_last_touch = {0};

/** 周期任务节点 */
static struct task_node s_periodic_task;

/** 周期任务运行标志 */
static bool s_periodic_running = false;

/* ========================================================================== */
/*                              内部函数                                       */
/* ========================================================================== */

/**
 * @brief 读取寄存器
 * 
 * @param reg_addr 寄存器地址
 * @param data 数据缓冲区
 * @param size 数据大小
 * @return CST816S_OK 成功
 * @return CST816S_ERR_I2C I2C错误
 */
static cst816s_err_t cst816s_read_reg(uint8_t reg_addr, uint8_t* data, size_t size) {
    size_t bytes_read = 0;
    i2c_drv_err_t ret = i2c_drv_read_reg(s_i2c_handle, CST816S_I2C_ADDR, reg_addr, data, size, &bytes_read);
    if (ret != I2C_DRV_OK) {
        LOGE(TAG, "Read reg 0x%02X failed: %d", reg_addr, ret);
        return CST816S_ERR_I2C;
    }
    
    if (bytes_read != size) {
        LOGE(TAG, "Read reg 0x%02X incomplete: %zu/%zu", reg_addr, bytes_read, size);
        return CST816S_ERR_I2C;
    }
    
    return CST816S_OK;
}

/**
 * @brief 写入寄存器
 * 
 * @param reg_addr 寄存器地址
 * @param data 数据缓冲区
 * @param size 数据大小
 * @return CST816S_OK 成功
 * @return CST816S_ERR_I2C I2C错误
 */
static cst816s_err_t cst816s_write_reg(uint8_t reg_addr, const uint8_t* data, size_t size) {
    i2c_drv_err_t ret = i2c_drv_write_reg(s_i2c_handle, CST816S_I2C_ADDR, reg_addr, data, size);
    if (ret != I2C_DRV_OK) {
        LOGE(TAG, "Write reg 0x%02X failed: %d", reg_addr, ret);
        return CST816S_ERR_I2C;
    }
    
    return CST816S_OK;
}

/**
 * @brief 硬件复位
 * 
 * @return CST816S_OK 成功
 */
static cst816s_err_t cst816s_hw_reset(void) {
    if (s_rst_pin < 0) {
        LOGW(TAG, "RST pin not configured, skip HW reset");
        return CST816S_OK;
    }
    
    LOGI(TAG, "Hardware reset, RST pin=%d", s_rst_pin);
    
    /* 配置RST引脚为输出 */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_rst_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    /* 拉低RST引脚 */
    gpio_set_level(s_rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    /* 拉高RST引脚 */
    gpio_set_level(s_rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    return CST816S_OK;
}

/**
 * @brief 读取触控数据
 * 
 * @param touch 输出参数，存储触控数据
 * @return CST816S_OK 成功
 * @return CST816S_ERR_NO_TOUCH 无触摸
 * @return CST816S_ERR_I2C I2C错误
 */
static cst816s_err_t cst816s_read_touch(cst816s_touch_t* touch) {
    if (!touch) {
        return CST816S_ERR_PARAM;
    }
    
    /* 读取寄存器: 手势 + 手指数量 + X坐标(2) + Y坐标(2) */
    uint8_t buf[6];
    cst816s_err_t ret = cst816s_read_reg(CST816S_REG_GESTURE, buf, 6);
    if (ret != CST816S_OK) {
        return ret;
    }
    
    /* 解析数据 */
    touch->gesture = (cst816s_gesture_t)buf[0];
    touch->finger_num = buf[1];
    touch->x = ((uint16_t)(buf[2] & 0x0F) << 8) | buf[3];
    touch->y = ((uint16_t)(buf[4] & 0x0F) << 8) | buf[5];
    touch->timestamp = (uint32_t)(esp_timer_get_time() / 1000);
    
    /* 检查是否有触摸 */
    if (touch->finger_num == 0) {
        return CST816S_ERR_NO_TOUCH;
    }
    
    return CST816S_OK;
}

/**
 * @brief 周期性检测任务回调
 * 
 * @param ctx 用户上下文 (未使用)
 * @return 任务状态
 */
static enum task_t cst816s_periodic_task_fn(void* ctx) {
    (void)ctx;
    
    cst816s_touch_t touch;
    cst816s_err_t ret = cst816s_read_touch(&touch);
    
    if (ret == CST816S_OK) {
        /* 更新最近一次数据 */
        s_last_touch = touch;
        
        /* 通过event_bus发布触控事件 */
        event_touch_t event_data = {
            .x = (int)touch.x,
            .y = (int)touch.y,
            .gesture = (touch_gesture_t)touch.gesture
        };
        
        /* 根据手势类型发布不同的事件 */
        switch (touch.gesture) {
            case CST816S_GESTURE_SINGLE_TAP:
            case CST816S_GESTURE_DOUBLE_TAP:
                EVENT_BUS_PUBLISH(EVENT_TOUCH_PRESS, &event_data);
                break;
            case CST816S_GESTURE_SLIDE_UP:
            case CST816S_GESTURE_SLIDE_DOWN:
            case CST816S_GESTURE_SLIDE_LEFT:
            case CST816S_GESTURE_SLIDE_RIGHT:
                EVENT_BUS_PUBLISH(EVENT_TOUCH_SWIPE, &event_data);
                break;
            case CST816S_GESTURE_LONG_PRESS:
                EVENT_BUS_PUBLISH(EVENT_TOUCH_LONG_PRESS, &event_data);
                break;
            default:
                break;
        }
        
        LOGD(TAG, "Touch: x=%d, y=%d, gesture=%d", 
             touch.x, touch.y, touch.gesture);
    } else if (ret != CST816S_ERR_NO_TOUCH) {
        /* 只在非"无触摸"错误时记录日志 */
        LOGW(TAG, "Read touch failed: %d", ret);
    }
    
    return TASK_OK;
}

/* ========================================================================== */
/*                              公共API实现                                    */
/* ========================================================================== */

cst816s_err_t cst816s_init(void) {
    if (s_initialized) {
        LOGW(TAG, "Already initialized");
        return CST816S_OK;
    }
    
    LOGI(TAG, "Initializing CST816S...");

    /* 按 compatible 定位自己的设备节点，父节点即所属 I2C 总线 */
    dtree_node_t* dev_node = dtree_find_by_compatible(CST816S_DT_COMPAT);
    if (!dev_node) {
        LOGE(TAG, "Device node '%s' not found in device tree", CST816S_DT_COMPAT);
        return CST816S_ERR_I2C;
    }
    dtree_node_t* bus_node = dtree_get_parent(dev_node);
    if (!bus_node) {
        LOGE(TAG, "Device node '%s' has no parent bus node", CST816S_DT_COMPAT);
        return CST816S_ERR_I2C;
    }

    /* 加载I2C总线配置（从父总线节点） */
    i2c_drv_config_t i2c_config;
    i2c_drv_err_t ret = i2c_drv_load_config(bus_node, &i2c_config);
    if (ret != I2C_DRV_OK) {
        LOGE(TAG, "Failed to load I2C config: %d", ret);
        return CST816S_ERR_I2C;
    }

    /* 初始化I2C驱动 (如果ath30已经初始化，会复用同一个I2C总线) */
    if (!s_i2c_handle) {
        ret = i2c_drv_init(&i2c_config, &s_i2c_handle);
        if (ret != I2C_DRV_OK) {
            LOGE(TAG, "Failed to init I2C: %d", ret);
            return CST816S_ERR_I2C;
        }
    }

    /* 从设备节点读取INT和RST引脚 */
    dtree_err_t dret;
    int32_t int_pin, rst_pin;

    dret = dtree_get_int(dev_node, "int_pin", &int_pin);
    if (dret == DTREE_OK) {
        s_int_pin = (int)int_pin;
        LOGI(TAG, "  INT pin: %d", s_int_pin);
    }

    dret = dtree_get_int(dev_node, "rst_pin", &rst_pin);
    if (dret == DTREE_OK) {
        s_rst_pin = (int)rst_pin;
        LOGI(TAG, "  RST pin: %d", s_rst_pin);
    }
    
    /* 硬件复位 */
    cst816s_hw_reset();
    
    /* 读取芯片ID验证通信 */
    uint8_t chip_id = 0;
    cst816s_err_t err = cst816s_read_reg(CST816S_REG_CHIP_ID, &chip_id, 1);
    if (err != CST816S_OK) {
        LOGE(TAG, "Failed to read chip ID: %d", err);
        if (s_i2c_handle) {
            i2c_drv_deinit(s_i2c_handle);
            s_i2c_handle = NULL;
        }
        return err;
    }
    
    LOGI(TAG, "  Chip ID: 0x%02X", chip_id);
    
    s_initialized = true;
    
    LOGI(TAG, "CST816S initialized successfully");
    LOGI(TAG, "  I2C address: 0x%02X", CST816S_I2C_ADDR);
    
    return CST816S_OK;
}

cst816s_err_t cst816s_deinit(void) {
    if (!s_initialized) {
        return CST816S_OK;
    }
    
    LOGI(TAG, "Deinitializing CST816S...");
    
    /* 停止周期任务 */
    cst816s_stop_periodic_read();
    
    /* 释放本模块持有的 I2C 总线引用（共享计数，归零才真正销毁） */
    if (s_i2c_handle) {
        i2c_drv_deinit(s_i2c_handle);
        s_i2c_handle = NULL;
    }
    
    s_initialized = false;
    
    LOGI(TAG, "CST816S deinitialized");
    
    return CST816S_OK;
}

cst816s_err_t cst816s_read(cst816s_touch_t* touch) {
    if (!s_initialized) {
        return CST816S_ERR_NOT_INIT;
    }
    
    if (!touch) {
        return CST816S_ERR_PARAM;
    }
    
    return cst816s_read_touch(touch);
}

cst816s_err_t cst816s_is_touched(bool* touched) {
    if (!s_initialized) {
        return CST816S_ERR_NOT_INIT;
    }
    
    if (!touched) {
        return CST816S_ERR_PARAM;
    }
    
    /* 读取手指数量 */
    uint8_t finger_num = 0;
    cst816s_err_t ret = cst816s_read_reg(CST816S_REG_FINGER_NUM, &finger_num, 1);
    if (ret != CST816S_OK) {
        return ret;
    }
    
    *touched = (finger_num > 0);
    
    return CST816S_OK;
}

cst816s_err_t cst816s_get_position(uint16_t* x, uint16_t* y) {
    if (!s_initialized) {
        return CST816S_ERR_NOT_INIT;
    }
    
    if (!x || !y) {
        return CST816S_ERR_PARAM;
    }
    
    /* 读取坐标寄存器 */
    uint8_t buf[4];
    cst816s_err_t ret = cst816s_read_reg(CST816S_REG_XPOS_H, buf, 4);
    if (ret != CST816S_OK) {
        return ret;
    }
    
    *x = ((uint16_t)(buf[0] & 0x0F) << 8) | buf[1];
    *y = ((uint16_t)(buf[2] & 0x0F) << 8) | buf[3];
    
    return CST816S_OK;
}

cst816s_err_t cst816s_get_gesture(cst816s_gesture_t* gesture) {
    if (!s_initialized) {
        return CST816S_ERR_NOT_INIT;
    }
    
    if (!gesture) {
        return CST816S_ERR_PARAM;
    }
    
    /* 读取手势寄存器 */
    uint8_t gesture_val = 0;
    cst816s_err_t ret = cst816s_read_reg(CST816S_REG_GESTURE, &gesture_val, 1);
    if (ret != CST816S_OK) {
        return ret;
    }
    
    *gesture = (cst816s_gesture_t)gesture_val;
    
    return CST816S_OK;
}

bool cst816s_is_initialized(void) {
    return s_initialized;
}

cst816s_err_t cst816s_start_periodic_read(uint32_t interval_ms) {
    if (!s_initialized) {
        return CST816S_ERR_NOT_INIT;
    }
    
    if (s_periodic_running) {
        LOGW(TAG, "Periodic read already running");
        return CST816S_OK;
    }
    
    if (interval_ms == 0) {
        interval_ms = CST816S_DEFAULT_INTERVAL_MS;
    }
    
    LOGI(TAG, "Starting periodic read, interval=%lu ms", interval_ms);
    
    /* 初始化周期任务 */
    tasker_task_init_mi(
        &s_periodic_task,
        (int)interval_ms,
        -1,  /* 无限运行 */
        "cst816s",
        cst816s_periodic_task_fn,
        NULL
    );
    
    /* 注册到tasker */
    int ret = tasker_enqueue(&s_periodic_task);
    if (ret != 0) {
        LOGE(TAG, "Failed to enqueue periodic task: %d", ret);
        return CST816S_ERR_I2C;
    }
    
    s_periodic_running = true;
    
    LOGI(TAG, "Periodic read started");
    
    return CST816S_OK;
}

cst816s_err_t cst816s_stop_periodic_read(void) {
    if (!s_periodic_running) {
        return CST816S_OK;
    }
    
    LOGI(TAG, "Stopping periodic read...");
    
    /* 取消任务 */
    task_cancel(&s_periodic_task);
    
    s_periodic_running = false;
    
    LOGI(TAG, "Periodic read stopped");
    
    return CST816S_OK;
}
