/**
 * @file audio_module.c
 * @brief 音频模块实现
 * 
 * 实现音频处理功能，包括麦克风录音和功放播放。
 * 使用I2S驱动进行硬件访问，通过event_bus发布音频状态事件。
 * 
 * @author OVS Team
 * @date 2026-09-07
 */

#include "audio_module.h"
#include "i2s_drv.h"
#include "event_bus.h"
#include "tasker.h"
#include "logger.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

static const char* TAG = "[AUDIO_MODULE]";

/* ========================================================================== */
/*                              常量定义                                       */
/* ========================================================================== */

/** 音频缓冲区大小（字节） */
#define AUDIO_BUFFER_SIZE       4096

/** 录音任务栈大小 */
#define RECORD_TASK_STACK_SIZE  4096

/* ========================================================================== */
/*                              内部数据结构                                   */
/* ========================================================================== */

/**
 * @brief 音频模块句柄结构
 */
struct audio_module_handle {
    i2s_drv_handle_t i2s_handle;     /**< I2S驱动句柄 */
    bool initialized;                /**< 初始化标志 */
    bool recording;                  /**< 录音状态 */
    bool playing;                    /**< 播放状态 */
    
    /* 录音相关 */
    TaskHandle_t record_task;        /**< 录音任务句柄 */
    uint8_t* record_buffer;          /**< 录音缓冲区 */
    size_t record_buffer_size;       /**< 录音缓冲区大小 */
};

/* ========================================================================== */
/*                              内部变量                                       */
/* ========================================================================== */

/** 全局音频模块句柄 */
static struct audio_module_handle* s_handle = NULL;

/* ========================================================================== */
/*                              内部函数                                       */
/* ========================================================================== */

/**
 * @brief 录音任务
 */
static void audio_record_task(void* arg) {
    struct audio_module_handle* handle = (struct audio_module_handle*)arg;
    
    LOGI(TAG, "Record task started");
    
    while (handle->recording) {
        size_t bytes_read = 0;
        i2s_drv_err_t err = i2s_drv_read(handle->i2s_handle, 
                                          handle->record_buffer, 
                                          handle->record_buffer_size, 
                                          &bytes_read);
        
        if (err == I2S_DRV_OK && bytes_read > 0) {
            /* 发布音频数据事件 */
            event_audio_data_t event_data = {
                .data = handle->record_buffer,
                .length = bytes_read,
                .sample_rate = 16000,
                .bits_per_sample = 16,
            };
            EVENT_BUS_PUBLISH(EVENT_AUDIO_DATA, &event_data);
        } else {
            /* 短暂延迟避免CPU占用过高 */
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    LOGI(TAG, "Record task stopped");
    
    /* 删除任务 */
    handle->record_task = NULL;
    vTaskDelete(NULL);
}

/* ========================================================================== */
/*                              公共API实现                                    */
/* ========================================================================== */

audio_module_err_t audio_module_init(audio_module_handle_t* handle) {
    if (!handle) {
        return AUDIO_MODULE_ERR_PARAM;
    }
    
    if (s_handle) {
        LOGW(TAG, "Already initialized");
        *handle = s_handle;
        return AUDIO_MODULE_OK;
    }
    
    LOGI(TAG, "Initializing audio module...");
    
    /* 分配句柄 */
    struct audio_module_handle* h = (struct audio_module_handle*)malloc(sizeof(struct audio_module_handle));
    if (!h) {
        LOGE(TAG, "Failed to allocate handle");
        return AUDIO_MODULE_ERR_HW;
    }
    memset(h, 0, sizeof(struct audio_module_handle));
    
    /* 加载I2S配置 */
    i2s_drv_config_t i2s_config;
    i2s_drv_err_t i2s_err = i2s_drv_load_config(&i2s_config);
    if (i2s_err != I2S_DRV_OK) {
        LOGE(TAG, "Failed to load I2S config: %d", i2s_err);
        free(h);
        return AUDIO_MODULE_ERR_HW;
    }
    
    /* 初始化I2S驱动 */
    i2s_err = i2s_drv_init(&i2s_config, &h->i2s_handle);
    if (i2s_err != I2S_DRV_OK) {
        LOGE(TAG, "Failed to init I2S: %d", i2s_err);
        free(h);
        return AUDIO_MODULE_ERR_HW;
    }
    
    /* 分配录音缓冲区 */
    h->record_buffer = (uint8_t*)malloc(AUDIO_BUFFER_SIZE);
    if (!h->record_buffer) {
        LOGE(TAG, "Failed to allocate record buffer");
        i2s_drv_deinit(h->i2s_handle);
        free(h);
        return AUDIO_MODULE_ERR_HW;
    }
    h->record_buffer_size = AUDIO_BUFFER_SIZE;
    
    h->initialized = true;
    s_handle = h;
    *handle = h;
    
    LOGI(TAG, "Audio module initialized successfully");
    
    /* 发布音频就绪事件 */
    EVENT_BUS_PUBLISH_EMPTY(EVENT_AUDIO_READY);
    
    return AUDIO_MODULE_OK;
}

audio_module_err_t audio_module_deinit(audio_module_handle_t handle) {
    if (!handle || !handle->initialized) {
        return AUDIO_MODULE_ERR_NOT_INIT;
    }
    
    LOGI(TAG, "Deinitializing audio module...");
    
    /* 停止录音 */
    audio_module_stop_recording(handle);
    
    /* 反初始化I2S */
    if (handle->i2s_handle) {
        i2s_drv_deinit(handle->i2s_handle);
        handle->i2s_handle = NULL;
    }
    
    /* 释放缓冲区 */
    if (handle->record_buffer) {
        free(handle->record_buffer);
        handle->record_buffer = NULL;
    }
    
    handle->initialized = false;
    
    if (s_handle == handle) {
        s_handle = NULL;
    }
    
    free(handle);
    
    LOGI(TAG, "Audio module deinitialized");
    
    return AUDIO_MODULE_OK;
}

audio_module_err_t audio_module_start_recording(audio_module_handle_t handle) {
    if (!handle || !handle->initialized) {
        return AUDIO_MODULE_ERR_NOT_INIT;
    }
    
    if (handle->recording) {
        LOGW(TAG, "Already recording");
        return AUDIO_MODULE_OK;
    }
    
    LOGI(TAG, "Starting recording...");
    
    handle->recording = true;
    
    /* 创建录音任务 */
    BaseType_t ret = xTaskCreate(
        audio_record_task,
        "audio_record",
        RECORD_TASK_STACK_SIZE,
        handle,
        5,
        &handle->record_task
    );
    
    if (ret != pdPASS) {
        LOGE(TAG, "Failed to create record task");
        handle->recording = false;
        return AUDIO_MODULE_ERR_HW;
    }
    
    LOGI(TAG, "Recording started");
    
    return AUDIO_MODULE_OK;
}

audio_module_err_t audio_module_stop_recording(audio_module_handle_t handle) {
    if (!handle || !handle->initialized) {
        return AUDIO_MODULE_ERR_NOT_INIT;
    }
    
    if (!handle->recording) {
        return AUDIO_MODULE_OK;
    }
    
    LOGI(TAG, "Stopping recording...");
    
    handle->recording = false;
    
    /* 等待任务结束 */
    if (handle->record_task) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    LOGI(TAG, "Recording stopped");
    
    return AUDIO_MODULE_OK;
}

audio_module_err_t audio_module_read(audio_module_handle_t handle, void* buffer, 
                                      size_t size, size_t* bytes_read) {
    if (!handle || !buffer || !bytes_read) {
        return AUDIO_MODULE_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return AUDIO_MODULE_ERR_NOT_INIT;
    }
    
    /* 直接从I2S读取 */
    i2s_drv_err_t err = i2s_drv_read(handle->i2s_handle, buffer, size, bytes_read);
    if (err != I2S_DRV_OK) {
        *bytes_read = 0;
        return AUDIO_MODULE_ERR_HW;
    }
    
    return AUDIO_MODULE_OK;
}

audio_module_err_t audio_module_play(audio_module_handle_t handle, const void* data, size_t size) {
    if (!handle || !data) {
        return AUDIO_MODULE_ERR_PARAM;
    }
    
    if (!handle->initialized) {
        return AUDIO_MODULE_ERR_NOT_INIT;
    }
    
    LOGI(TAG, "Playing %zu bytes...", size);
    
    handle->playing = true;
    
    /* 写入I2S */
    size_t bytes_written = 0;
    i2s_drv_err_t err = i2s_drv_write(handle->i2s_handle, data, size, &bytes_written);
    
    handle->playing = false;
    
    if (err != I2S_DRV_OK) {
        LOGE(TAG, "Play failed: %d", err);
        return AUDIO_MODULE_ERR_HW;
    }
    
    LOGI(TAG, "Playback complete: %zu bytes", bytes_written);
    
    return AUDIO_MODULE_OK;
}

audio_module_err_t audio_module_stop_playback(audio_module_handle_t handle) {
    if (!handle || !handle->initialized) {
        return AUDIO_MODULE_ERR_NOT_INIT;
    }
    
    /* I2S没有直接的停止API，这里只是设置标志 */
    handle->playing = false;
    
    LOGI(TAG, "Playback stopped");
    
    return AUDIO_MODULE_OK;
}
