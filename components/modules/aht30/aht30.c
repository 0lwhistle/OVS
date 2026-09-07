/**
 * @file aht30.c
 * @brief AHT30 温湿度传感器模块实现
 * 
 * 实现AHT30温湿度传感器的初始化、数据读取功能。
 * 使用I2C总线通信，通过event_bus发布温湿度数据。
 * 使用tasker进行周期性数据采集。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "aht30.h"
#include "i2c_drv.h"
#include "event_bus.h"
#include "tasker.h"
#include "logger.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

static const char* TAG = "[AHT30]";

/* ========================================================================== */
/*                              常量定义                                       */
/* ========================================================================== */

/** AHT30 I2C地址 */
#define AHT30_I2C_ADDR          0x38

/** AHT30 命令 */
#define AHT30_CMD_INIT          0xBE
#define AHT30_CMD_TRIGGER       0xAC
#define AHT30_CMD_SOFT_RESET    0xBA

/** AHT30 状态位 */
#define AHT30_STATUS_BUSY       0x80
#define AHT30_STATUS_CALIBRATED 0x08

/** 默认采集间隔 (ms) */
#define AHT30_DEFAULT_INTERVAL_MS   1000

/* ========================================================================== */
/*                              内部变量                                       */
/* ========================================================================== */

/** I2C驱动句柄 */
static i2c_drv_handle_t s_i2c_handle = NULL;

/** 初始化标志 */
static bool s_initialized = false;

/** 最近一次数据 */
static aht30_data_t s_last_data = {0};

/** 周期任务节点 */
static struct task_node s_periodic_task;

/** 周期任务运行标志 */
static bool s_periodic_running = false;

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
static uint8_t aht30_crc8(const uint8_t* data, size_t len) {
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
 * @return AHT30_OK 成功
 * @return AHT30_ERR_I2C I2C错误
 */
static aht30_err_t aht30_soft_reset(void) {
    uint8_t cmd = AHT30_CMD_SOFT_RESET;
    
    i2c_drv_err_t ret = i2c_drv_write(s_i2c_handle, AHT30_I2C_ADDR, &cmd, 1);
    if (ret != I2C_DRV_OK) {
        LOGE(TAG, "Soft reset failed: %d", ret);
        return AHT30_ERR_I2C;
    }
    
    /* 等待复位完成 */
    vTaskDelay(pdMS_TO_TICKS(20));
    
    return AHT30_OK;
}

/**
 * @brief 初始化AHT30传感器
 * 
 * @return AHT30_OK 成功
 * @return AHT30_ERR_I2C I2C错误
 */
static aht30_err_t aht30_hw_init(void) {
    uint8_t cmd[3] = {AHT30_CMD_INIT, 0x08, 0x00};
    
    i2c_drv_err_t ret = i2c_drv_write(s_i2c_handle, AHT30_I2C_ADDR, cmd, 3);
    if (ret != I2C_DRV_OK) {
        LOGE(TAG, "HW init failed: %d", ret);
        return AHT30_ERR_I2C;
    }
    
    /* 等待初始化完成 */
    vTaskDelay(pdMS_TO_TICKS(10));
    
    return AHT30_OK;
}

/**
 * @brief 触发测量并读取数据
 * 
 * @param data 输出参数，存储温湿度数据
 * @return AHT30_OK 成功
 * @return AHT30_ERR_I2C I2C错误
 * @return AHT30_ERR_CRC CRC校验失败
 * @return AHT30_ERR_BUSY 传感器忙
 */
static aht30_err_t aht30_measure(aht30_data_t* data) {
    if (!data) {
        return AHT30_ERR_PARAM;
    }
    
    /* 发送触发测量命令 */
    uint8_t cmd[3] = {AHT30_CMD_TRIGGER, 0x33, 0x00};
    i2c_drv_err_t ret = i2c_drv_write(s_i2c_handle, AHT30_I2C_ADDR, cmd, 3);
    if (ret != I2C_DRV_OK) {
        LOGE(TAG, "Trigger measure failed: %d", ret);
        return AHT30_ERR_I2C;
    }
    
    /* 等待测量完成 (至少80ms) */
    vTaskDelay(pdMS_TO_TICKS(100));
    
    /* 读取数据 (7字节: 状态 + 湿度[3] + 温度[2] + CRC) */
    uint8_t buf[7];
    size_t bytes_read = 0;
    ret = i2c_drv_read(s_i2c_handle, AHT30_I2C_ADDR, buf, 7, &bytes_read);
    if (ret != I2C_DRV_OK) {
        LOGE(TAG, "Read data failed: %d", ret);
        return AHT30_ERR_I2C;
    }
    
    if (bytes_read != 7) {
        LOGE(TAG, "Read data incomplete: %zu/7", bytes_read);
        return AHT30_ERR_I2C;
    }
    
    /* 检查状态位 */
    if (buf[0] & AHT30_STATUS_BUSY) {
        LOGW(TAG, "Sensor busy");
        return AHT30_ERR_BUSY;
    }
    
    /* CRC校验 (对前6字节) */
    uint8_t crc = aht30_crc8(buf, 6);
    if (crc != buf[6]) {
        LOGE(TAG, "CRC error: expected=0x%02X, got=0x%02X", buf[6], crc);
        return AHT30_ERR_CRC;
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
    
    return AHT30_OK;
}

/**
 * @brief 周期性采集任务回调
 * 
 * @param ctx 用户上下文 (未使用)
 * @return 任务状态
 */
static enum task_t aht30_periodic_task_fn(void* ctx) {
    (void)ctx;
    
    aht30_data_t data;
    aht30_err_t ret = aht30_measure(&data);
    
    if (ret == AHT30_OK) {
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
    } else {
        LOGW(TAG, "Measure failed: %d", ret);
        
        /* 发布传感器错误事件 */
        EVENT_BUS_PUBLISH_EMPTY(EVENT_SENSOR_ERROR);
    }
    
    return TASK_OK;
}

/* ========================================================================== */
/*                              公共API实现                                    */
/* ========================================================================== */

aht30_err_t aht30_init(void) {
    if (s_initialized) {
        LOGW(TAG, "Already initialized");
        return AHT30_OK;
    }
    
    LOGI(TAG, "Initializing AHT30...");
    
    /* 加载I2C配置 */
    i2c_drv_config_t i2c_config;
    i2c_drv_err_t ret = i2c_drv_load_config(&i2c_config);
    if (ret != I2C_DRV_OK) {
        LOGE(TAG, "Failed to load I2C config: %d", ret);
        return AHT30_ERR_I2C;
    }
    
    /* 初始化I2C驱动 */
    ret = i2c_drv_init(&i2c_config, &s_i2c_handle);
    if (ret != I2C_DRV_OK) {
        LOGE(TAG, "Failed to init I2C: %d", ret);
        return AHT30_ERR_I2C;
    }
    
    /* 软复位 */
    aht30_err_t err = aht30_soft_reset();
    if (err != AHT30_OK) {
        LOGE(TAG, "Soft reset failed: %d", err);
        i2c_drv_deinit(s_i2c_handle);
        s_i2c_handle = NULL;
        return err;
    }
    
    /* 硬件初始化 */
    err = aht30_hw_init();
    if (err != AHT30_OK) {
        LOGE(TAG, "HW init failed: %d", err);
        i2c_drv_deinit(s_i2c_handle);
        s_i2c_handle = NULL;
        return err;
    }
    
    s_initialized = true;
    
    LOGI(TAG, "AHT30 initialized successfully");
    LOGI(TAG, "  I2C address: 0x%02X", AHT30_I2C_ADDR);
    
    return AHT30_OK;
}

aht30_err_t aht30_deinit(void) {
    if (!s_initialized) {
        return AHT30_OK;
    }
    
    LOGI(TAG, "Deinitializing AHT30...");
    
    /* 停止周期任务 */
    aht30_stop_periodic_read();
    
    /* 反初始化I2C */
    if (s_i2c_handle) {
        i2c_drv_deinit(s_i2c_handle);
        s_i2c_handle = NULL;
    }
    
    s_initialized = false;
    
    LOGI(TAG, "AHT30 deinitialized");
    
    return AHT30_OK;
}

aht30_err_t aht30_read(aht30_data_t* data) {
    if (!s_initialized) {
        return AHT30_ERR_NOT_INIT;
    }
    
    if (!data) {
        return AHT30_ERR_PARAM;
    }
    
    return aht30_measure(data);
}

aht30_err_t aht30_get_temperature(float* temperature) {
    if (!s_initialized) {
        return AHT30_ERR_NOT_INIT;
    }
    
    if (!temperature) {
        return AHT30_ERR_PARAM;
    }
    
    *temperature = s_last_data.temperature;
    
    return AHT30_OK;
}

aht30_err_t aht30_get_humidity(float* humidity) {
    if (!s_initialized) {
        return AHT30_ERR_NOT_INIT;
    }
    
    if (!humidity) {
        return AHT30_ERR_PARAM;
    }
    
    *humidity = s_last_data.humidity;
    
    return AHT30_OK;
}

bool aht30_is_initialized(void) {
    return s_initialized;
}

aht30_err_t aht30_start_periodic_read(uint32_t interval_ms) {
    if (!s_initialized) {
        return AHT30_ERR_NOT_INIT;
    }
    
    if (s_periodic_running) {
        LOGW(TAG, "Periodic read already running");
        return AHT30_OK;
    }
    
    if (interval_ms == 0) {
        interval_ms = AHT30_DEFAULT_INTERVAL_MS;
    }
    
    LOGI(TAG, "Starting periodic read, interval=%lu ms", interval_ms);
    
    /* 初始化周期任务 */
    tasker_task_init_mi(
        &s_periodic_task,
        (int)interval_ms,
        -1,  /* 无限运行 */
        "aht30",
        aht30_periodic_task_fn,
        NULL
    );
    
    /* 注册到tasker */
    int ret = tasker_enqueue(&s_periodic_task);
    if (ret != 0) {
        LOGE(TAG, "Failed to enqueue periodic task: %d", ret);
        return AHT30_ERR_I2C;
    }
    
    s_periodic_running = true;
    
    LOGI(TAG, "Periodic read started");
    
    return AHT30_OK;
}

aht30_err_t aht30_stop_periodic_read(void) {
    if (!s_periodic_running) {
        return AHT30_OK;
    }
    
    LOGI(TAG, "Stopping periodic read...");
    
    /* 取消任务 */
    task_cancel(&s_periodic_task);
    
    s_periodic_running = false;
    
    LOGI(TAG, "Periodic read stopped");
    
    return AHT30_OK;
}
