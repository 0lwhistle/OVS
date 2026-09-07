/**
 * @file audio_module.h
 * @brief 音频模块接口
 * 
 * 提供音频处理功能，包括麦克风录音和功放播放。
 * 使用I2S驱动进行硬件访问。
 */

#ifndef AUDIO_MODULE_H
#define AUDIO_MODULE_H

#include <stdint.h>
#include <stdbool.h>
#include "i2s_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    AUDIO_MODULE_OK = 0,
    AUDIO_MODULE_ERR_NOT_INIT = -1,
    AUDIO_MODULE_ERR_PARAM = -2,
    AUDIO_MODULE_ERR_HW = -3,
    AUDIO_MODULE_ERR_NO_DATA = -4,
} audio_module_err_t;

/* ========== 类型定义 ========== */
typedef struct audio_module_config {
    i2s_drv_config_t i2s_config;
    int32_t buffer_size;
    int32_t sample_rate;
    int32_t bits_per_sample;
} audio_module_config_t;

typedef struct audio_module_handle* audio_module_handle_t;

/* ========== 公共 API ========== */

/**
 * @brief 初始化音频模块
 * 
 * @param handle 输出参数，存储模块句柄
 * @return audio_module_err_t 错误码
 */
audio_module_err_t audio_module_init(audio_module_handle_t* handle);

/**
 * @brief 反初始化音频模块
 * 
 * @param handle 模块句柄
 * @return audio_module_err_t 错误码
 */
audio_module_err_t audio_module_deinit(audio_module_handle_t handle);

/**
 * @brief 开始录音
 * 
 * @param handle 模块句柄
 * @return audio_module_err_t 错误码
 */
audio_module_err_t audio_module_start_recording(audio_module_handle_t handle);

/**
 * @brief 停止录音
 * 
 * @param handle 模块句柄
 * @return audio_module_err_t 错误码
 */
audio_module_err_t audio_module_stop_recording(audio_module_handle_t handle);

/**
 * @brief 读取音频数据
 * 
 * @param handle 模块句柄
 * @param buffer 数据缓冲区
 * @param size 缓冲区大小（字节）
 * @param bytes_read 实际读取的字节数
 * @return audio_module_err_t 错误码
 */
audio_module_err_t audio_module_read(audio_module_handle_t handle, void* buffer, size_t size, size_t* bytes_read);

/**
 * @brief 播放音频数据
 * 
 * @param handle 模块句柄
 * @param data 音频数据
 * @param size 数据大小（字节）
 * @return audio_module_err_t 错误码
 */
audio_module_err_t audio_module_play(audio_module_handle_t handle, const void* data, size_t size);

/**
 * @brief 停止播放
 * 
 * @param handle 模块句柄
 * @return audio_module_err_t 错误码
 */
audio_module_err_t audio_module_stop_playback(audio_module_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_MODULE_H */
