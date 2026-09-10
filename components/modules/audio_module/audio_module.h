/**
 * @file audio_module.h
 * @brief 音频模块接口（PCM 设备抽象层）
 *
 * 职责（idle_modules_plan §2.2 分层）：≈ALSA pcm core——异步写（环形缓冲
 * + tasker 喂数）、软件音量缩放（int16 饱和）、SD 引脚硬静音、drain/abort、
 * 播放完成回调。播放策略（队列/文件/解码/事件）在 audio_player 层。
 *
 * 兼容性：init/deinit/录音 API 与既有行为保持一致（录音路径不得回归）。
 * 硬件参数从设备树读取（i2s-amplifier 节点 sd_pin、audio 节点策略键）。
 */

#ifndef AUDIO_MODULE_H
#define AUDIO_MODULE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
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
    /* [GUI] T2 追加（既有值不变，只增不改） */
    AUDIO_MODULE_ERR_NO_SPACE = -5,   /**< 环形缓冲剩余空间不足，稍后重试 */
    AUDIO_MODULE_ERR_TIMEOUT = -6,    /**< drain 等待超时 */
    AUDIO_MODULE_ERR_BUSY = -7,       /**< 播放通道被占用 */
} audio_module_err_t;

/* ========== 类型定义 ========== */
typedef struct audio_module_config {
    i2s_drv_config_t i2s_config;
    int32_t buffer_size;
    int32_t sample_rate;
    int32_t bits_per_sample;
} audio_module_config_t;

typedef struct audio_module_handle* audio_module_handle_t;

/** 异步流结束状态（完成回调参数） */
typedef enum {
    AUDIO_MODULE_STREAM_DONE = 0,     /**< 缓冲数据全部写完（正常收尾） */
    AUDIO_MODULE_STREAM_ABORTED,      /**< 被abort丢弃 */
    AUDIO_MODULE_STREAM_ERROR,        /**< 硬件写失败中断 */
} audio_module_stream_status_t;

/** 异步流完成回调（tasker worker 上下文调用，勿做重活/阻塞） */
typedef void (*audio_module_play_done_cb_t)(audio_module_stream_status_t status,
                                            uint32_t total_bytes,
                                            void* user_data);

/* ========== 公共 API ========== */

/**
 * @brief 初始化音频模块（幂等：重复调用返回同一句柄）
 *
 * 从设备树加载 I2S 与静音脚配置；SD 脚缺省 -1 表示未接（仅软件静音）。
 */
audio_module_err_t audio_module_init(audio_module_handle_t* handle);

/**
 * @brief 反初始化音频模块
 */
audio_module_err_t audio_module_deinit(audio_module_handle_t handle);

/* ---------------- 录音（既有路径，保持兼容） ---------------- */

audio_module_err_t audio_module_start_recording(audio_module_handle_t handle);
audio_module_err_t audio_module_stop_recording(audio_module_handle_t handle);
audio_module_err_t audio_module_read(audio_module_handle_t handle, void* buffer, size_t size, size_t* bytes_read);

/* ---------------- 播放 ---------------- */

/**
 * @brief 播放音频数据（阻塞版本）
 * @deprecated T2 起建议改用 audio_module_write_async() + drain()；
 *             本接口保留一个版本周期后删除。
 */
audio_module_err_t audio_module_play(audio_module_handle_t handle, const void* data, size_t size);

/**
 * @brief 异步写播放数据（拷贝进内部环形缓冲后立即返回）
 *
 * 缓冲满返回 AUDIO_MODULE_ERR_NO_SPACE（本次未写入），调用方稍后重试；
 * 首次成功写入自动启动 tasker Little 喂数任务。
 * 约定：仅 16bit PCM；音量/静音在喂数出队时施加。
 */
audio_module_err_t audio_module_write_async(audio_module_handle_t handle,
                                            const void* data, size_t size);

/**
 * @brief 标记流结束（此后缓冲播完即触发完成回调，不阻塞）
 */
audio_module_err_t audio_module_play_finish(audio_module_handle_t handle);

/**
 * @brief 标记流结束并阻塞等待缓冲播完（超时返回 AUDIO_MODULE_ERR_TIMEOUT）
 */
audio_module_err_t audio_module_drain(audio_module_handle_t handle, uint32_t timeout_ms);

/**
 * @brief 中止当前流：丢弃缓冲数据并触发 ABORTED 回调
 */
audio_module_err_t audio_module_abort_playback(audio_module_handle_t handle);

/**
 * @brief 注册异步流完成回调（NULL 注销；在流进行中更换作用于下一次收尾）
 */
audio_module_err_t audio_module_set_play_done_cb(audio_module_handle_t handle,
                                                 audio_module_play_done_cb_t cb,
                                                 void* user_data);

/**
 * @brief 软件音量 0~100（int16 饱和缩放，出队时施加）
 */
audio_module_err_t audio_module_set_volume(audio_module_handle_t handle, uint8_t volume);
audio_module_err_t audio_module_get_volume(audio_module_handle_t handle, uint8_t* out_volume);

/**
 * @brief 静音：SD 脚关断（若已接）+ 出队数据清零双保险
 */
audio_module_err_t audio_module_mute(audio_module_handle_t handle, bool mute);
audio_module_err_t audio_module_is_muted(audio_module_handle_t handle, bool* out_muted);

/**
 * @brief 停止播放（阻塞版通道的兼容接口；异步流请用 abort_playback）
 */
audio_module_err_t audio_module_stop_playback(audio_module_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_MODULE_H */
