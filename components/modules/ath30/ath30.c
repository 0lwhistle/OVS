/**
 * @file ath30.c
 * @brief ath30 温湿度传感器模块实现
 * 
 * 实现ath30温湿度传感器的初始化、数据读取功能。
 * 使用I2C总线通信，通过event_bus发布温湿度数据。
 * 使用tasker进行周期性数据采集。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "ath30.h"
#include "i2c_drv.h"
#include "event_bus.h"
#include "tasker.h"
#include "logger.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

static const char* TAG = "[ath30]";

/* 本模块服务的设备（设备树 compatible），初始化时按它查找自己的节点 */
#define ath30_DT_COMPAT   "ath30-sensor"

/* ========================================================================== */
/*                              常量定义                                       */
/* ========================================================================== */

/** ath30 I2C地址 */
#define ath30_I2C_ADDR          0x38

/** ath30 命令 */
#define AHT30_CMD_INIT          0xBE
#define ath30_CMD_TRIGGER       0xAC
#define ath30_CMD_SOFT_RESET    0xBA

/** ath30 状态位 */
#define ath30_STATUS_BUSY       0x80
#define ath30_STATUS_CALIBRATED 0x08

/** 默认采集间隔 (ms)：设备树 sample_interval_ms 缺省兜底（idle_modules_plan §3） */
#define ath30_DEFAULT_INTERVAL_MS   30000

/** 连续失败 N 次后停采并发布 ERROR 事件 */
#define ath30_FAIL_LIMIT            3

/** 停采后每 N 个周期重试一次，成功即恢复 */
#define ath30_RETRY_PERIODS         5

/* ========================================================================== */
/*                              内部变量                                       */
/* ========================================================================== */

/** I2C驱动句柄 */
static i2c_drv_handle_t s_i2c_handle = NULL;

/** 初始化标志 */
static bool s_initialized = false;

/** 当前芯片是否输出 CRC 字节（AHT30=true / AHT10=false） */
static bool s_has_crc = true;

/** 最近一次数据 */
static ath30_data_t s_last_data = {0};

/** 周期任务节点 */
static struct task_node s_periodic_task;

/** 周期任务运行标志 */
static bool s_periodic_running = false;

/** 连续失败计数（达到 ath30_FAIL_LIMIT 停采） */
static int s_fail_count = 0;

/** 停采标志（停采后每 ath30_RETRY_PERIODS 个周期重试一次） */
static bool s_sampling_stopped = false;

/** 停采后已跳过的周期数 */
static int s_retry_skip = 0;

/* ========================================================================== */
/*                              内部函数                                       */
/* ========================================================================== */

/**
 * @brief CRC8校验
 * 
 * @param data 数据指针
 * @param len 数据长度
 * @return CRC8值
 */
static uint8_t ath30_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

/**
 * @brief 发送软复位命令
 * 
 * @return ath30_OK 成功
 * @return ath30_ERR_I2C I2C错误
 */
static ath30_err_t ath30_soft_reset(void) {
    uint8_t cmd = ath30_CMD_SOFT_RESET;
    
    i2c_drv_err_t ret = i2c_drv_write(s_i2c_handle, ath30_I2C_ADDR, &cmd, 1);
    if (ret != I2C_DRV_OK) {
        LOGE(TAG, "Soft reset failed: %d", ret);
        return ath30_ERR_I2C;
    }
    
    /* 等待复位完成 */
    vTaskDelay(pdMS_TO_TICKS(20));
    
    return ath30_OK;
}

/**
 * @brief 初始化ath30传感器
 * 
 * @return ath30_OK 成功
 * @return ath30_ERR_I2C I2C错误
 */
static ath30_err_t ath30_hw_init(void) {
    uint8_t cmd[3] = {AHT30_CMD_INIT, 0x08, 0x00};
    
    i2c_drv_err_t ret = i2c_drv_write(s_i2c_handle, ath30_I2C_ADDR, cmd, 3);
    if (ret != I2C_DRV_OK) {
        LOGE(TAG, "HW init failed: %d", ret);
        return ath30_ERR_I2C;
    }
    
    /* 等待初始化完成 */
    vTaskDelay(pdMS_TO_TICKS(10));
    
    return ath30_OK;
}

/**
 * @brief 触发测量并读取数据
 * 
 * @param data 输出参数，存储温湿度数据
 * @return ath30_OK 成功
 * @return ath30_ERR_I2C I2C错误
 * @return ath30_ERR_CRC CRC校验失败
 * @return ath30_ERR_BUSY 传感器忙
 */
static ath30_err_t ath30_measure(ath30_data_t* data) {
    if (!data) {
        return ath30_ERR_PARAM;
    }
    
    /* 发送触发测量命令 */
    uint8_t cmd[3] = {ath30_CMD_TRIGGER, 0x33, 0x00};
    i2c_drv_err_t ret = i2c_drv_write(s_i2c_handle, ath30_I2C_ADDR, cmd, 3);
    if (ret != I2C_DRV_OK) {
        LOGE(TAG, "Trigger measure failed: %d", ret);
        return ath30_ERR_I2C;
    }
    
    /* 等待测量完成（忙位轮询，参考实现至少 75ms） */
    vTaskDelay(pdMS_TO_TICKS(80));
    for (int i = 0; i < 5; i++) {
        uint8_t status = 0;
        size_t got = 0;
        if (i2c_drv_read(s_i2c_handle, ath30_I2C_ADDR, &status, 1, &got) == I2C_DRV_OK &&
            !(status & ath30_STATUS_BUSY)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    /* 读取数据 (7字节: 状态 + 湿度[3] + 温度[2] + CRC) */
    uint8_t buf[7];
    size_t bytes_read = 0;
    ret = i2c_drv_read(s_i2c_handle, ath30_I2C_ADDR, buf, 7, &bytes_read);
    if (ret != I2C_DRV_OK) {
        LOGE(TAG, "Read data failed: %d", ret);
        return ath30_ERR_I2C;
    }
    
    if (bytes_read != 7) {
        LOGE(TAG, "Read data incomplete: %zu/7", bytes_read);
        return ath30_ERR_I2C;
    }
    
    /* 检查状态位 */
    if (buf[0] & ath30_STATUS_BUSY) {
        LOGW(TAG, "Sensor busy");
        return ath30_ERR_BUSY;
    }
    
    /* CRC校验 (对前6字节) */
    uint8_t crc = ath30_crc8(buf, 6);
    if (crc != buf[6]) {
        LOGE(TAG, "CRC error: expected=0x%02X, got=0x%02X", buf[6], crc);
        return ath30_ERR_CRC;
    }
    
    /* 解析湿度数据 (20位) */
    uint32_t humidity_raw = ((uint32_t)buf[1] << 12) | 
                            ((uint32_t)buf[2] << 4) | 
                            ((uint32_t)buf[3] >> 4);
    
    /* 解析温度数据 (20位) */
    uint32_t temperature_raw = (((uint32_t)buf[3] & 0x0F) << 16) | 
                               ((uint32_t)buf[4] << 8) | 
                               (uint32_t)buf[5];
    
    /* 转换为实际值 */
    data->humidity = (float)humidity_raw / 1048576.0f * 100.0f;
    data->temperature = (float)temperature_raw / 1048576.0f * 200.0f - 50.0f;
    data->timestamp = (uint32_t)(esp_timer_get_time() / 1000);
    
    return ath30_OK;
}

/**
 * @brief 周期性采集任务回调
 * 
 * @param ctx 用户上下文 (未使用)
 * @return 任务状态
 */
static enum task_t ath30_periodic_task_fn(void* ctx) {
    (void)ctx;

    /* 停采状态：跳过 ath30_RETRY_PERIODS-1 个周期，第 N 个周期重试一次 */
    if (s_sampling_stopped) {
        if (++s_retry_skip < ath30_RETRY_PERIODS) {
            return TASK_OK;
        }
        s_retry_skip = 0;
        LOGI(TAG, "retrying measure after %d failures", s_fail_count);
    }

    ath30_data_t data;
    ath30_err_t ret = ath30_measure(&data);

    if (ret == ath30_OK) {
        if (s_sampling_stopped) {
            LOGI(TAG, "sensor recovered, sampling resumed");
        }
        s_fail_count = 0;
        s_sampling_stopped = false;

        /* 更新最近一次数据 */
        s_last_data = data;

        /* 通过event_bus发布温湿度数据 */
        event_sensor_temp_humidity_t event_data = {
            .temperature = data.temperature,
            .humidity = data.humidity,
            .timestamp = data.timestamp
        };
        EVENT_BUS_PUBLISH(EVENT_SENSOR_TEMP_HUMIDITY, &event_data);

        LOGD(TAG, "Temp: %.1f°C, Humidity: %.1f%%",
             data.temperature, data.humidity);
    } else if (!s_sampling_stopped) {
        s_fail_count++;
        if (s_fail_count >= ath30_FAIL_LIMIT) {
            /* 连续失败达限：发布 ERROR 并停采（之后周期性重试恢复） */
            LOGW(TAG, "measure failed %d times, sampling suspended (%d)",
                 s_fail_count, ret);
            s_sampling_stopped = true;
            s_retry_skip = 0;
            EVENT_BUS_PUBLISH_EMPTY(EVENT_SENSOR_ERROR);
        } else {
            LOGW(TAG, "measure failed (%d), %d/%d", ret, s_fail_count,
                 ath30_FAIL_LIMIT);
        }
    } else {
        LOGD(TAG, "retry measure failed: %d", ret);
    }

    return TASK_OK;
}

/* ========================================================================== */
/*                              公共API实现                                    */
/* ========================================================================== */

ath30_err_t ath30_init(void) {
    if (s_initialized) {
        LOGW(TAG, "Already initialized");
        return ath30_OK;
    }
    
    LOGI(TAG, "Initializing ath30...");

    /* 按 compatible 定位自己的设备节点，父节点即所属 I2C 总线 */
    dtree_node_t* dev_node = dtree_find_by_compatible(ath30_DT_COMPAT);
    if (!dev_node) {
        LOGE(TAG, "Device node '%s' not found in device tree", ath30_DT_COMPAT);
        return ath30_ERR_I2C;
    }
    dtree_node_t* bus_node = dtree_get_parent(dev_node);
    if (!bus_node) {
        LOGE(TAG, "Device node '%s' has no parent bus node", ath30_DT_COMPAT);
        return ath30_ERR_I2C;
    }

    /* 加载I2C总线配置（从父总线节点） */
    i2c_drv_config_t i2c_config;
    i2c_drv_err_t ret = i2c_drv_load_config(bus_node, &i2c_config);
    if (ret != I2C_DRV_OK) {
        LOGE(TAG, "Failed to load I2C config: %d", ret);
        return ath30_ERR_I2C;
    }
    
    /* 初始化I2C驱动 */
    ret = i2c_drv_init(&i2c_config, &s_i2c_handle);
    if (ret != I2C_DRV_OK) {
        LOGE(TAG, "Failed to init I2C: %d", ret);
        return ath30_ERR_I2C;
    }
    
    /* 软复位 */
    ath30_err_t err = ath30_soft_reset();
    if (err != ath30_OK) {
        LOGE(TAG, "Soft reset failed: %d", err);
        i2c_drv_deinit(s_i2c_handle);
        s_i2c_handle = NULL;
        return err;
    }
    
    /* 硬件初始化 */
    err = ath30_hw_init();
    if (err != ath30_OK) {
        LOGE(TAG, "HW init failed: %d", err);
        i2c_drv_deinit(s_i2c_handle);
        s_i2c_handle = NULL;
        return err;
    }
    
    s_initialized = true;

    LOGI(TAG, "ath30 initialized successfully");
    LOGI(TAG, "  I2C address: 0x%02X", ath30_I2C_ADDR);

    /* 收口（idle_modules_plan §3）：init 即启动周期采集（tasker Middle），
     * 间隔=设备树 sample_interval_ms → 兼容旧键 measure_interval_ms →
     * 缺省 30000+LOGW */
    int32_t interval = 0;
    if (dtree_get_int(dev_node, "sample_interval_ms", &interval) != DTREE_OK ||
        interval <= 0) {
        if (dtree_get_int(dev_node, "measure_interval_ms", &interval) != DTREE_OK ||
            interval <= 0) {
            interval = ath30_DEFAULT_INTERVAL_MS;
            LOGW(TAG, "sample_interval_ms not set, using default %d ms",
                 (int)interval);
        }
    }
    ath30_start_periodic_read((uint32_t)interval);

    return ath30_OK;
}

ath30_err_t ath30_deinit(void) {
    if (!s_initialized) {
        return ath30_OK;
    }
    
    LOGI(TAG, "Deinitializing ath30...");
    
    /* 停止周期任务 */
    ath30_stop_periodic_read();
    
    /* 反初始化I2C */
    if (s_i2c_handle) {
        i2c_drv_deinit(s_i2c_handle);
        s_i2c_handle = NULL;
    }
    
    s_initialized = false;
    
    LOGI(TAG, "ath30 deinitialized");
    
    return ath30_OK;
}

ath30_err_t ath30_read(ath30_data_t* data) {
    if (!s_initialized) {
        return ath30_ERR_NOT_INIT;
    }
    
    if (!data) {
        return ath30_ERR_PARAM;
    }
    
    return ath30_measure(data);
}

ath30_err_t ath30_get_temperature(float* temperature) {
    if (!s_initialized) {
        return ath30_ERR_NOT_INIT;
    }
    
    if (!temperature) {
        return ath30_ERR_PARAM;
    }
    
    *temperature = s_last_data.temperature;
    
    return ath30_OK;
}

ath30_err_t ath30_get_humidity(float* humidity) {
    if (!s_initialized) {
        return ath30_ERR_NOT_INIT;
    }
    
    if (!humidity) {
        return ath30_ERR_PARAM;
    }
    
    *humidity = s_last_data.humidity;
    
    return ath30_OK;
}

bool ath30_is_initialized(void) {
    return s_initialized;
}

ath30_err_t ath30_start_periodic_read(uint32_t interval_ms) {
    if (!s_initialized) {
        return ath30_ERR_NOT_INIT;
    }
    
    if (s_periodic_running) {
        LOGW(TAG, "Periodic read already running");
        return ath30_OK;
    }
    
    if (interval_ms == 0) {
        interval_ms = ath30_DEFAULT_INTERVAL_MS;
    }
    
    LOGI(TAG, "Starting periodic read, interval=%lu ms", interval_ms);

    s_fail_count = 0;
    s_sampling_stopped = false;
    s_retry_skip = 0;

    /* 初始化周期任务 */
    tasker_task_init_mi(
        &s_periodic_task,
        (int)interval_ms,
        -1,  /* 无限运行 */
        "ath30",
        ath30_periodic_task_fn,
        NULL
    );
    
    /* 注册到tasker */
    int ret = tasker_enqueue(&s_periodic_task);
    if (ret != 0) {
        LOGE(TAG, "Failed to enqueue periodic task: %d", ret);
        return ath30_ERR_I2C;
    }
    
    s_periodic_running = true;
    
    LOGI(TAG, "Periodic read started");
    
    return ath30_OK;
}

ath30_err_t ath30_stop_periodic_read(void) {
    if (!s_periodic_running) {
        return ath30_OK;
    }
    
    LOGI(TAG, "Stopping periodic read...");
    
    /* 取消任务 */
    task_cancel(&s_periodic_task);
    
    s_periodic_running = false;
    
    LOGI(TAG, "Periodic read stopped");
    
    return ath30_OK;
}
