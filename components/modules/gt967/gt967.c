/**
 * @file gt967.c
 * @brief GT967 单层 5 点电容触控模块实现（GT9xx 系协议）
 *
 * 寄存器为 16 位地址（如 0x814E），本组件用
 * "i2c 写 2 字节寄存器指针 + 读" 访问（GT9xx 支持写-停-读）。
 * 复位时序兼做 I2C 地址选择：INT 保持低 → 0x5D；INT 保持高 → 0x14。
 * 无手势引擎（单层多点），上报告只映射为 TOUCH_GESTURE_PRESS，
 * 手势语义归 UI 层（navigator/页面）按轨迹自行判断。
 */

#include "gt967.h"
#include "i2c_drv.h"
#include "event_bus.h"
#include "tasker.h"
#include "logger.h"
#include "dtree.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

#include <string.h>
#include <stdio.h>

static const char* TAG = "[GT967]";

/* 本模块服务的设备（设备树 compatible） */
#define GT967_DT_COMPAT     "gt967-touch"

/* ===== GT9xx 寄存器（16 位地址） ===== */
#define GT967_REG_PRODUCT_ID    0x8140u  /* 4B ASCII，如 "9671" */
#define GT967_REG_STATUS        0x814Eu  /* bit7 就绪，低 4 位点数 */
#define GT967_REG_POINTS        0x8150u  /* 每点 8B：tid,xl,xh,yl,yh,sl,sh,rsv */
#define GT967_POINT_SIZE        8u
#define GT967_MAX_POINTS        5u

/* 缺省轮询间隔 */
#define GT967_DEFAULT_INTERVAL_MS   20

/* 候选 I2C 地址（7 位） */
static const uint8_t GT967_ADDRS[2] = {0x5D, 0x14};

static i2c_drv_handle_t s_i2c_handle = NULL;
static uint8_t s_i2c_addr = 0;
static int s_int_pin = -1;
static int s_rst_pin = -1;
static bool s_initialized = false;

static struct task_node s_periodic_task;
static bool s_periodic_running = false;

/* 坐标修正（设备树可选键，默认关闭；上板联调方向不对时改设备树） */
static bool s_swap_xy = false;
static bool s_mirror_x = false;
static bool s_mirror_y = false;
static int32_t s_max_x = 240;
static int32_t s_max_y = 280;

/* ========================================================================== */
/*                              底层访问                                       */
/* ========================================================================== */

/** 写 2 字节寄存器指针 + 读数据（GT9xx 写-停-读时序） */
static gt967_err_t gt967_read_regs(uint16_t reg, uint8_t* data, size_t len) {
    if (!s_i2c_handle) {
        return GT967_ERR_NOT_INIT;
    }
    uint8_t ptr[2] = {(uint8_t)(reg & 0xFF), (uint8_t)(reg >> 8)};
    i2c_drv_err_t ret = i2c_drv_write(s_i2c_handle, s_i2c_addr, ptr, sizeof(ptr));
    if (ret != I2C_DRV_OK) {
        return GT967_ERR_I2C;
    }
    size_t bytes_read = 0;
    ret = i2c_drv_read(s_i2c_handle, s_i2c_addr, data, len, &bytes_read);
    if (ret != I2C_DRV_OK || bytes_read != len) {
        return GT967_ERR_I2C;
    }
    return GT967_OK;
}

static gt967_err_t gt967_write_reg(uint16_t reg, uint8_t value) {
    if (!s_i2c_handle) {
        return GT967_ERR_NOT_INIT;
    }
    uint8_t buf[3] = {(uint8_t)(reg & 0xFF), (uint8_t)(reg >> 8), value};
    i2c_drv_err_t ret = i2c_drv_write(s_i2c_handle, s_i2c_addr, buf, sizeof(buf));
    return (ret == I2C_DRV_OK) ? GT967_OK : GT967_ERR_I2C;
}

static void pin_output(int pin, int level) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(pin, level);
}

static void pin_float_input(int pin) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

/**
 * @brief GT9xx 复位时序（兼地址选择）
 *
 * INT 在 RST 上升沿保持低 → 地址 0x5D；保持高 → 0x14。
 */
static void gt967_hw_reset(bool addr_0x14) {
    if (s_rst_pin < 0) {
        LOGW(TAG, "RST pin not configured, skip address-select reset");
        return;
    }
    if (s_int_pin < 0) {
        LOGW(TAG, "INT pin not configured, cannot select address by timing");
    }

    pin_output(s_rst_pin, 0);
    if (s_int_pin >= 0) {
        pin_output(s_int_pin, addr_0x14 ? 1 : 0);
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    pin_output(s_rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    if (s_int_pin >= 0) {
        pin_float_input(s_int_pin);   /* INT 恢复浮空（中断线） */
    }
    vTaskDelay(pdMS_TO_TICKS(60));    /* 芯片内部初始化等待 */
}

/** I2C 总线恢复：SCL 手动打 9 个脉冲，直到 SDA 被从机释放 */
static void gt967_bus_recovery(void) {
    /* 复用 RST/INT 之外——需要直接操纵 SCL/SDA；从总线父节点拿引脚 */
    dtree_node_t* dev = dtree_find_by_compatible(GT967_DT_COMPAT);
    if (!dev) {
        return;
    }
    dtree_node_t* bus = dtree_get_parent(dev);
    int32_t sda = -1, scl = -1;
    if (dtree_get_int(bus, "sda_pin", &sda) != DTREE_OK ||
        dtree_get_int(bus, "scl_pin", &scl) != DTREE_OK) {
        LOGW(TAG, "bus recovery: sda/scl pins not in device tree, skip");
        return;
    }
    LOGW(TAG, "i2c bus recovery on SDA=%ld SCL=%ld", (long)sda, (long)scl);

    /* SCL 输出、SDA 输入上拉，打 9 个时钟 */
    gpio_config_t out_conf = {
        .pin_bit_mask = (1ULL << scl),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&out_conf);
    gpio_config_t in_conf = {
        .pin_bit_mask = (1ULL << sda),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&in_conf);

    for (int i = 0; i < 9; i++) {
        gpio_set_level(scl, 0);
        esp_rom_delay_us(5);
        gpio_set_level(scl, 1);
        esp_rom_delay_us(5);
        if (gpio_get_level(sda)) {
            LOGW(TAG, "bus recovered after %d SCL pulses", i + 1);
            break;
        }
    }
    /* 收尾 STOP 条件 */
    gpio_set_level(scl, 0);
    esp_rom_delay_us(5);
    /* 恢复：交给 i2c 控制器重新 init 由驱动层处理（这里仅诊断用） */
}

/** 探测可用的 I2C 地址（读产品 ID 校验通信） */
static gt967_err_t gt967_probe(void) {
    for (int attempt = 0; attempt < 2; attempt++) {
        bool want_0x14 = (attempt == 1);
        gt967_hw_reset(want_0x14);

        for (int i = 0; i < 2; i++) {
            s_i2c_addr = GT967_ADDRS[i];
            uint8_t id[4] = {0};
            if (gt967_read_regs(GT967_REG_PRODUCT_ID, id, sizeof(id)) == GT967_OK &&
                id[0] >= '0' && id[0] <= '9') {
                LOGI(TAG, "probe ok: addr=0x%02X product_id=%.4s",
                     s_i2c_addr, (const char*)id);
                return GT967_OK;
            }
        }
    }
    s_i2c_addr = 0;

    /* 总线恢复：SCL 打 9 个脉冲解锁被从机钳住的 SDA（真机诊断） */
    gt967_bus_recovery();

    /* 诊断：全地址扫描（任意 ACK 即说明总线通） */
    LOGW(TAG, "i2c bus scan 0x03-0x77:");
    int found = 0;
    for (uint8_t a = 0x03; a <= 0x77; a++) {
        uint8_t dummy = 0x00;
        if (i2c_drv_write(s_i2c_handle, a, &dummy, 1) == I2C_DRV_OK) {
            LOGW(TAG, "  ACK at 0x%02X", a);
            found++;
        }
    }
    if (!found) {
        LOGE(TAG, "no device ACKs on i2c bus (check SDA=16/SCL=17 soldering, pull-ups, sensor power)");
    }
    return GT967_ERR_I2C;
}

/* ========================================================================== */
/*                              触点读取与事件                                  */
/* ========================================================================== */

static gt967_err_t gt967_read_touch(gt967_touch_t* touch) {
    if (!touch) {
        return GT967_ERR_PARAM;
    }
    memset(touch, 0, sizeof(*touch));

    uint8_t status = 0;
    gt967_err_t ret = gt967_read_regs(GT967_REG_STATUS, &status, 1);
    if (ret != GT967_OK) {
        return ret;
    }
    if (!(status & 0x80)) {
        return GT967_ERR_NO_TOUCH;   /* 缓冲未就绪 */
    }

    uint8_t point_num = status & 0x0F;
    /* 清状态位（读后必须清零，否则芯片不再更新） */
    gt967_write_reg(GT967_REG_STATUS, 0x00);

    if (point_num == 0 || point_num > GT967_MAX_POINTS) {
        return GT967_ERR_NO_TOUCH;
    }

    uint8_t buf[GT967_MAX_POINTS * GT967_POINT_SIZE];
    ret = gt967_read_regs(GT967_REG_POINTS, buf, (size_t)point_num * GT967_POINT_SIZE);
    if (ret != GT967_OK) {
        return ret;
    }

    touch->point_num = point_num;
    touch->timestamp = (uint32_t)(esp_timer_get_time() / 1000);
    for (int i = 0; i < point_num; i++) {
        const uint8_t* p = &buf[i * GT967_POINT_SIZE];
        uint16_t x = (uint16_t)p[1] | ((uint16_t)(p[2] & 0x0F) << 8);
        uint16_t y = (uint16_t)p[3] | ((uint16_t)(p[4] & 0x0F) << 8);

        /* 设备树可配的坐标修正（上板方向联调用） */
        int32_t fx = x, fy = y;
        if (s_swap_xy) {
            int32_t t = fx; fx = fy; fy = t;
        }
        if (s_mirror_x) {
            fx = s_max_x - fx;
        }
        if (s_mirror_y) {
            fy = s_max_y - fy;
        }

        touch->points[i].track_id = p[0];
        touch->points[i].area = p[5];
        touch->points[i].x = (uint16_t)(fx < 0 ? 0 : (fx >= s_max_x ? s_max_x - 1 : fx));
        touch->points[i].y = (uint16_t)(fy < 0 ? 0 : (fy >= s_max_y ? s_max_y - 1 : fy));
    }
    return GT967_OK;
}

static enum task_t gt967_periodic_task_fn(void* ctx) {
    (void)ctx;

    gt967_touch_t touch;
    gt967_err_t ret = gt967_read_touch(&touch);
    if (ret != GT967_OK) {
        if (ret != GT967_ERR_NO_TOUCH) {
            LOGW(TAG, "read touch failed: %d", ret);
        }
        return TASK_OK;
    }

    /* 首触点上报（多点语义后续按页面需求扩展） */
    event_touch_t event_data = {
        .x = touch.points[0].x,
        .y = touch.points[0].y,
        .gesture = TOUCH_GESTURE_PRESS,
    };
    EVENT_BUS_PUBLISH(EVENT_TOUCH_PRESS, &event_data);

    LOGD(TAG, "touch: n=%u p0=(%u,%u) id=%u",
         touch.point_num, touch.points[0].x, touch.points[0].y,
         touch.points[0].track_id);
    return TASK_OK;
}

/* ========================================================================== */
/*                              公共API实现                                    */
/* ========================================================================== */

gt967_err_t gt967_init(void) {
    if (s_initialized) {
        LOGW(TAG, "Already initialized");
        return GT967_OK;
    }
    LOGI(TAG, "Initializing GT967 touch...");

    dtree_node_t* dev_node = dtree_find_by_compatible(GT967_DT_COMPAT);
    if (!dev_node) {
        LOGE(TAG, "Device node '%s' not found in device tree", GT967_DT_COMPAT);
        return GT967_ERR_I2C;
    }
    dtree_node_t* bus_node = dtree_get_parent(dev_node);
    if (!bus_node) {
        LOGE(TAG, "Device node has no parent bus node");
        return GT967_ERR_I2C;
    }

    i2c_drv_config_t i2c_config;
    if (i2c_drv_load_config(bus_node, &i2c_config) != I2C_DRV_OK ||
        i2c_drv_init(&i2c_config, &s_i2c_handle) != I2C_DRV_OK) {
        LOGE(TAG, "I2C bus init failed");
        s_i2c_handle = NULL;
        return GT967_ERR_I2C;
    }

    int32_t v = 0;
    if (dtree_get_int(dev_node, "int_pin", &v) == DTREE_OK) {
        s_int_pin = (int)v;
        LOGI(TAG, "  INT pin: %d", s_int_pin);
    }
    if (dtree_get_int(dev_node, "rst_pin", &v) == DTREE_OK) {
        s_rst_pin = (int)v;
        LOGI(TAG, "  RST pin: %d", s_rst_pin);
    }
    s_swap_xy = (dtree_get_int(dev_node, "swap_xy", &v) == DTREE_OK && v == 1);
    s_mirror_x = (dtree_get_int(dev_node, "mirror_x", &v) == DTREE_OK && v == 1);
    s_mirror_y = (dtree_get_int(dev_node, "mirror_y", &v) == DTREE_OK && v == 1);
    if (dtree_get_int(dev_node, "max_x", &v) == DTREE_OK && v > 0) {
        s_max_x = v;
    }
    if (dtree_get_int(dev_node, "max_y", &v) == DTREE_OK && v > 0) {
        s_max_y = v;
    }

    gt967_err_t err = gt967_probe();
    if (err != GT967_OK) {
        LOGE(TAG, "probe failed on both addresses (wiring? addr 0x5D/0x14)");
        i2c_drv_deinit(s_i2c_handle);
        s_i2c_handle = NULL;
        return err;
    }

    s_initialized = true;
    LOGI(TAG, "GT967 initialized successfully (I2C 0x%02X, %ldx%ld)",
         s_i2c_addr, (long)s_max_x, (long)s_max_y);

    /* 初始化即启动轮询（20ms，触控响应优先） */
    gt967_start_periodic_read(GT967_DEFAULT_INTERVAL_MS);
    return GT967_OK;
}

gt967_err_t gt967_deinit(void) {
    if (!s_initialized) {
        return GT967_OK;
    }
    LOGI(TAG, "Deinitializing GT967...");

    gt967_stop_periodic_read();

    if (s_i2c_handle) {
        i2c_drv_deinit(s_i2c_handle);
        s_i2c_handle = NULL;
    }
    s_i2c_addr = 0;
    s_initialized = false;

    LOGI(TAG, "GT967 deinitialized");
    return GT967_OK;
}

gt967_err_t gt967_read(gt967_touch_t* touch) {
    if (!s_initialized) {
        return GT967_ERR_NOT_INIT;
    }
    return gt967_read_touch(touch);
}

bool gt967_is_initialized(void) {
    return s_initialized;
}

gt967_err_t gt967_start_periodic_read(uint32_t interval_ms) {
    if (!s_initialized) {
        return GT967_ERR_NOT_INIT;
    }
    if (s_periodic_running) {
        return GT967_OK;
    }
    if (interval_ms == 0) {
        interval_ms = GT967_DEFAULT_INTERVAL_MS;
    }
    LOGI(TAG, "Starting periodic read, interval=%lu ms", (unsigned long)interval_ms);

    tasker_task_init_mi(&s_periodic_task, (int)interval_ms, -1, "gt967",
                        gt967_periodic_task_fn, NULL);
    if (tasker_enqueue(&s_periodic_task) != 0) {
        LOGE(TAG, "Failed to enqueue periodic task");
        return GT967_ERR_I2C;
    }
    s_periodic_running = true;
    return GT967_OK;
}

gt967_err_t gt967_stop_periodic_read(void) {
    if (!s_periodic_running) {
        return GT967_OK;
    }
    task_cancel(&s_periodic_task);
    s_periodic_running = false;
    LOGI(TAG, "Periodic read stopped");
    return GT967_OK;
}
