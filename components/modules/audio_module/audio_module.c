/**
 * @file audio_module.c
 * @brief 音频模块实现（PCM 设备抽象层）
 *
 * 录音：既有 FreeRTOS 任务路径保持不变（经 audio_port 移植层创建）。
 * 播放：T2 异步引擎——环形缓冲 + tasker Little 喂数 + int16 饱和音量
 * 缩放 + SD 引脚硬静音 + drain/abort + 完成回调。播放策略（队列/文件/
 * 解码/事件）归 audio_player，本层不发布播放事件。
 *
 * @author OVS Team
 * @date 2026-09-07 (T2 演进 2026-09-10)
 */

#include "audio_module.h"
#include "audio_port.h"
#include "mem.h"
#include "i2s_drv.h"
#include "event_bus.h"
#include "tasker.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static const char* TAG = "[AUDIO_MODULE]";

/* ========================================================================== */
/*                              常量定义                                       */
/* ========================================================================== */

/** 录音缓冲区大小（字节） */
#define AUDIO_BUFFER_SIZE       4096

/** 录音任务栈大小 */
#define RECORD_TASK_STACK_SIZE  4096

/** 异步喂数任务周期（ms）：tasker Little 级 */
#define AUDIO_FEED_PERIOD_MS    20

/** 每次喂数最大字节数（约 16kHz/16bit/mono 的 32ms） */
#define AUDIO_FEED_CHUNK_SIZE   1024

/** 环形缓冲默认帧数（16bit 单帧=2 字节；设备树 audio.buf_frames 可覆盖） */
#define AUDIO_RING_DEFAULT_FRAMES 2048

/** 音量默认值（设备树 audio.volume_default 可覆盖） */
#define AUDIO_VOLUME_DEFAULT    80

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
    bool playing;                    /**< 播放状态（阻塞版通道） */

    /* 录音相关 */
    void* record_task;               /**< 录音任务句柄（移植层） */
    uint8_t* record_buffer;          /**< 录音缓冲区 */
    size_t record_buffer_size;       /**< 录音缓冲区大小 */

    /* ---- T2 异步播放 ---- */
    audio_lock_t* lock;              /**< 保护异步状态与环形缓冲 */
    uint8_t* ring;                   /**< 环形缓冲（生产者拷入，喂数任务取走） */
    uint32_t ring_size;              /**< 环形缓冲容量（字节） */
    uint32_t rd;                     /**< 读计数（仅喂数任务推进） */
    uint32_t wr;                     /**< 写计数（write_async 推进） */

    bool playing_stream;             /**< 有异步流进行中 */
    bool finish_pending;             /**< 已标记流结束（播完即收尾） */
    bool abort_pending;              /**< 已请求中止 */
    bool feed_running;               /**< 喂数任务在调度器中存活 */
    uint32_t total_written;          /**< 本流已写入 I2S 的字节数 */

    uint8_t volume;                  /**< 软件音量 0~100 */
    bool muted;                      /**< 静音标志 */
    int32_t sd_pin;                  /**< SD 静音脚（<0 未接） */

    audio_module_play_done_cb_t done_cb;   /**< 异步流完成回调 */
    void* done_cb_arg;
};

/* ========================================================================== */
/*                              内部变量                                       */
/* ========================================================================== */

/** 全局音频模块句柄 */
static struct audio_module_handle* s_handle = NULL;

/** 喂数暂存块（单实例喂任务独占，禁止在持锁时使用） */
static uint8_t s_feed_chunk[AUDIO_FEED_CHUNK_SIZE];

/* ========================================================================== */
/*                              内部函数                                       */
/* ========================================================================== */

/**
 * @brief 录音任务（既有路径，行为不变；经移植层创建）
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
            audio_sleep_ms(10);
        }
    }

    LOGI(TAG, "Record task stopped");
}

/**
 * @brief 环形缓冲已用字节数（须持锁）
 */
static uint32_t ring_used(const struct audio_module_handle* h) {
    return h->wr - h->rd;
}

/**
 * @brief int16 饱和音量缩放：out = sample * volume / 100，溢出钳位
 */
static inline int16_t audio_scale_sample(int16_t sample, uint8_t volume) {
    if (volume >= 100) {
        return sample;
    }
    int32_t scaled = ((int32_t)sample * (int32_t)volume) / 100;
    if (scaled > 32767) {
        scaled = 32767;
    } else if (scaled < -32768) {
        scaled = -32768;
    }
    return (int16_t)scaled;
}

/**
 * @brief 从环形缓冲弹出到 s_feed_chunk 并施加音量/静音（须持锁；不推进 rd）
 * @return 弹出字节数
 */
static uint32_t feed_pop_locked(struct audio_module_handle* h) {
    uint32_t used = ring_used(h);
    uint32_t n = used < AUDIO_FEED_CHUNK_SIZE ? used : AUDIO_FEED_CHUNK_SIZE;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t raw = h->ring[(h->rd + i) % h->ring_size];
        s_feed_chunk[i] = raw;
    }
    if (n > 0 && (h->muted || h->volume < 100) && (n % 2 == 0)) {
        /* 16bit PCM 逐样本饱和缩放（字节序内小端，低字节在前） */
        for (uint32_t i = 0; i + 1 < n; i += 2) {
            int16_t s = (int16_t)((uint16_t)s_feed_chunk[i] |
                                  ((uint16_t)s_feed_chunk[i + 1] << 8));
            int16_t scaled = h->muted ? 0 : audio_scale_sample(s, h->volume);
            s_feed_chunk[i] = (uint8_t)((uint16_t)scaled & 0xFFu);
            s_feed_chunk[i + 1] = (uint8_t)(((uint16_t)scaled >> 8) & 0xFFu);
        }
    }
    return n;
}

/**
 * @brief 收尾公共路径（须持锁）：停喂数任务、置空闲、带出回调信息
 * @param status 本次流的结束状态
 * @param total_out 带出本流已写字节数
 */
static void feed_finish_locked(struct audio_module_handle* h,
                               audio_module_stream_status_t status,
                               uint32_t* total_out,
                               audio_module_play_done_cb_t* cb_out,
                               void** cb_arg_out) {
    h->feed_running = false;
    h->playing_stream = false;
    h->finish_pending = false;
    h->abort_pending = false;
    tasker_cancel_by_name("audio_feed");
    (void)status;
    *total_out = h->total_written;
    *cb_out = h->done_cb;
    *cb_arg_out = h->done_cb_arg;
}

static enum task_t audio_feed_tick(void* arg);

/**
 * @brief 启动喂数任务（须持锁；tasker 队列满时返回 false，调用方稍后重试）
 */
static bool feed_start_locked(struct audio_module_handle* h) {
    if (h->feed_running) {
        return true;
    }
    struct task_node node;
    if (tasker_task_init_li(&node, AUDIO_FEED_PERIOD_MS, -1,
                            "audio_feed", audio_feed_tick, h) == TASK_OK &&
        tasker_enqueue(&node) == TASK_OK) {
        h->feed_running = true;
        return true;
    }
    LOGE(TAG, "Failed to start feed task (queue full?)");
    return false;
}

/**
 * @brief 异步喂数任务（tasker Little 周期任务；无数据时空转等待）
 */
static enum task_t audio_feed_tick(void* arg) {
    struct audio_module_handle* h = (struct audio_module_handle*)arg;

    uint32_t n = 0;
    bool fire = false;
    audio_module_stream_status_t status = AUDIO_MODULE_STREAM_DONE;
    uint32_t total = 0;
    audio_module_play_done_cb_t cb = NULL;
    void* cb_arg = NULL;

    if (!audio_lock_take(h->lock, 100)) {
        return TASK_OK;
    }

    if (h->abort_pending) {
        h->rd = h->wr;   /* 丢弃缓冲 */
        feed_finish_locked(h, AUDIO_MODULE_STREAM_ABORTED, &total, &cb, &cb_arg);
        status = AUDIO_MODULE_STREAM_ABORTED;
        fire = true;
    } else {
        n = feed_pop_locked(h);
        bool empty = (ring_used(h) == 0);
        if (empty && h->finish_pending) {
            feed_finish_locked(h, AUDIO_MODULE_STREAM_DONE, &total, &cb, &cb_arg);
            total += n;   /* 最后一批即将写出 */
            fire = true;
        }
    }

    audio_lock_give(h->lock);

    if (n > 0) {
        h->rd += n;   /* 仅喂数任务推进 rd，无需持锁 */
        size_t bytes_written = 0;
        i2s_drv_err_t err = i2s_drv_write(h->i2s_handle, s_feed_chunk, n, &bytes_written);
        if (err != I2S_DRV_OK) {
            LOGE(TAG, "Async feed I2S write failed: %d", err);
            if (audio_lock_take(h->lock, 100)) {
                h->rd = h->wr;   /* 丢弃残余，避免残留半流 */
                feed_finish_locked(h, AUDIO_MODULE_STREAM_ERROR, &total, &cb, &cb_arg);
                status = AUDIO_MODULE_STREAM_ERROR;
                audio_lock_give(h->lock);
                fire = true;
            }
        } else {
            h->total_written += bytes_written;
        }
    }

    if (fire && cb) {
        cb(status, total, cb_arg);
    }

    return TASK_OK;
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
    struct audio_module_handle* h = (struct audio_module_handle*)mem_malloc(sizeof(struct audio_module_handle));
    if (!h) {
        LOGE(TAG, "Failed to allocate handle");
        return AUDIO_MODULE_ERR_HW;
    }
    memset(h, 0, sizeof(struct audio_module_handle));

    /* 按 compatible 定位麦克风和功放设备节点 */
    dtree_node_t* mic_node = dtree_find_by_compatible("i2s-microphone");
    dtree_node_t* amp_node = dtree_find_by_compatible("i2s-amplifier");
    if (!mic_node || !amp_node) {
        LOGE(TAG, "I2S device nodes not found (mic=%p, amp=%p)",
             (void*)mic_node, (void*)amp_node);
        mem_free(h);
        return AUDIO_MODULE_ERR_HW;
    }

    /* 麦克风所在总线即 I2S 总线 */
    dtree_node_t* bus_node = dtree_get_parent(mic_node);
    if (!bus_node) {
        LOGE(TAG, "I2S microphone node has no parent bus node");
        mem_free(h);
        return AUDIO_MODULE_ERR_HW;
    }

    /* 加载I2S总线配置，再从设备节点填充 DIN/DOUT 引脚 */
    i2s_drv_config_t i2s_config;
    i2s_drv_err_t i2s_err = i2s_drv_load_config(bus_node, &i2s_config);
    if (i2s_err != I2S_DRV_OK) {
        LOGE(TAG, "Failed to load I2S config: %d", i2s_err);
        mem_free(h);
        return AUDIO_MODULE_ERR_HW;
    }

    dtree_get_int(mic_node, "data_in_pin", &i2s_config.data_in_pin);
    dtree_get_int(amp_node, "data_out_pin", &i2s_config.data_out_pin);
    LOGI(TAG, "Audio devices: din=%" PRId32 " (mic), dout=%" PRId32 " (amp)",
         i2s_config.data_in_pin, i2s_config.data_out_pin);

    /* T2: SD 静音脚（功放节点，缺省 -1=未接） */
    h->sd_pin = -1;
    if (dtree_get_int(amp_node, "sd_pin", &h->sd_pin) != DTREE_OK) {
        h->sd_pin = -1;
    }

    /* T2: 播放策略键（audio 节点，缺省兜底） */
    int32_t buf_frames = AUDIO_RING_DEFAULT_FRAMES;
    int32_t volume_default = AUDIO_VOLUME_DEFAULT;
    dtree_node_t* policy = dtree_find_by_compatible("ovs-audio-policy");
    if (policy) {
        if (dtree_get_int(policy, "buf_frames", &buf_frames) != DTREE_OK || buf_frames <= 0) {
            buf_frames = AUDIO_RING_DEFAULT_FRAMES;
        }
        if (dtree_get_int(policy, "volume_default", &volume_default) != DTREE_OK ||
            volume_default < 0 || volume_default > 100) {
            volume_default = AUDIO_VOLUME_DEFAULT;
        }
    }

    /* 初始化I2S驱动 */
    i2s_err = i2s_drv_init(&i2s_config, &h->i2s_handle);
    if (i2s_err != I2S_DRV_OK) {
        LOGE(TAG, "Failed to init I2S: %d", i2s_err);
        mem_free(h);
        return AUDIO_MODULE_ERR_HW;
    }

    /* 分配录音缓冲区 */
    h->record_buffer = (uint8_t*)mem_malloc(AUDIO_BUFFER_SIZE);
    if (!h->record_buffer) {
        LOGE(TAG, "Failed to allocate record buffer");
        i2s_drv_deinit(h->i2s_handle);
        mem_free(h);
        return AUDIO_MODULE_ERR_HW;
    }
    h->record_buffer_size = AUDIO_BUFFER_SIZE;

    /* T2: 异步播放引擎资源 */
    h->ring_size = (uint32_t)buf_frames * 2u;   /* 16bit = 2 字节/帧 */
    h->ring = (uint8_t*)mem_malloc(h->ring_size);
    if (!h->ring) {
        LOGE(TAG, "Failed to allocate async ring (%u bytes)", (unsigned)h->ring_size);
        mem_free(h->record_buffer);
        i2s_drv_deinit(h->i2s_handle);
        mem_free(h);
        return AUDIO_MODULE_ERR_HW;
    }
    h->lock = audio_lock_create();
    if (!h->lock) {
        LOGE(TAG, "Failed to create async lock");
        mem_free(h->ring);
        mem_free(h->record_buffer);
        i2s_drv_deinit(h->i2s_handle);
        mem_free(h);
        return AUDIO_MODULE_ERR_HW;
    }
    h->volume = (uint8_t)volume_default;
    h->sd_pin = (h->sd_pin >= 0 && audio_port_sd_init(h->sd_pin, true)) ? h->sd_pin : -1;

    h->initialized = true;
    s_handle = h;
    *handle = h;

    LOGI(TAG, "Audio module initialized successfully (ring=%uB, vol=%u, sd_pin=%" PRId32 ")",
         (unsigned)h->ring_size, h->volume, h->sd_pin);

    /* 发布音频就绪事件 */
    EVENT_BUS_PUBLISH_EMPTY(EVENT_AUDIO_READY);

    return AUDIO_MODULE_OK;
}

audio_module_err_t audio_module_deinit(audio_module_handle_t handle) {
    if (!handle || !handle->initialized) {
        return AUDIO_MODULE_ERR_NOT_INIT;
    }

    LOGI(TAG, "Deinitializing audio module...");

    /* 停录音 */
    audio_module_stop_recording(handle);

    /* T2: 停异步播放并释放引擎 */
    audio_module_abort_playback(handle);
    audio_sleep_ms(30);   /* 等待喂数任务退出当前 tick */
    audio_lock_destroy(handle->lock);
    handle->lock = NULL;
    mem_free(handle->ring);
    handle->ring = NULL;

    /* 反初始化I2S */
    if (handle->i2s_handle) {
        i2s_drv_deinit(handle->i2s_handle);
        handle->i2s_handle = NULL;
    }

    /* 释放缓冲区 */
    if (handle->record_buffer) {
        mem_free(handle->record_buffer);
        handle->record_buffer = NULL;
    }

    handle->initialized = false;

    if (s_handle == handle) {
        s_handle = NULL;
    }

    mem_free(handle);

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

    /* 创建录音任务（经移植层：ESP=FreeRTOS / PC=pthread） */
    if (!audio_task_create("audio_record", RECORD_TASK_STACK_SIZE, 5,
                           audio_record_task, handle, &handle->record_task)) {
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

    /* 等待任务退出 */
    if (handle->record_task) {
        audio_sleep_ms(100);
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

    LOGI(TAG, "Playing %zu bytes... (deprecated blocking path)", size);

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

/* ---------------- T2 异步播放 ---------------- */

audio_module_err_t audio_module_write_async(audio_module_handle_t handle,
                                            const void* data, size_t size) {
    if (!handle || !data) {
        return AUDIO_MODULE_ERR_PARAM;
    }
    if (!handle->initialized || !handle->lock) {
        return AUDIO_MODULE_ERR_NOT_INIT;
    }
    if (size == 0) {
        return AUDIO_MODULE_OK;
    }

    audio_module_err_t ret = AUDIO_MODULE_OK;

    if (!audio_lock_take(handle->lock, 100)) {
        return AUDIO_MODULE_ERR_BUSY;
    }

    if (handle->finish_pending || handle->abort_pending) {
        /* 上一流收尾中，禁止交叉写入 */
        audio_lock_give(handle->lock);
        return AUDIO_MODULE_ERR_BUSY;
    }

    uint32_t free_space = handle->ring_size - ring_used(handle);
    if ((uint32_t)size > free_space) {
        audio_lock_give(handle->lock);
        return AUDIO_MODULE_ERR_NO_SPACE;
    }

    /* 分段拷入环形缓冲 */
    uint32_t tail_room = handle->ring_size - (handle->wr % handle->ring_size);
    uint32_t first = (uint32_t)size < tail_room ? (uint32_t)size : tail_room;
    memcpy(handle->ring + (handle->wr % handle->ring_size), data, first);
    if ((uint32_t)size > first) {
        memcpy(handle->ring, (const uint8_t*)data + first, (uint32_t)size - first);
    }
    handle->wr += (uint32_t)size;

    if (!handle->playing_stream) {
        handle->playing_stream = true;
        handle->total_written = 0;
    }

    if (!handle->feed_running) {
        if (!feed_start_locked(handle)) {
            ret = AUDIO_MODULE_ERR_BUSY;   /* 数据已入缓冲，下次 write_async/drain 重试启动 */
        }
    }

    audio_lock_give(handle->lock);
    return ret;
}

audio_module_err_t audio_module_play_finish(audio_module_handle_t handle) {
    if (!handle) {
        return AUDIO_MODULE_ERR_PARAM;
    }
    if (!handle->initialized || !handle->lock) {
        return AUDIO_MODULE_ERR_NOT_INIT;
    }

    audio_module_err_t ret = AUDIO_MODULE_OK;
    if (!audio_lock_take(handle->lock, 100)) {
        return AUDIO_MODULE_ERR_BUSY;
    }
    if (!handle->playing_stream) {
        ret = AUDIO_MODULE_ERR_NOT_INIT;   /* 无流可收尾 */
    } else {
        handle->finish_pending = true;
    }
    audio_lock_give(handle->lock);
    return ret;
}

audio_module_err_t audio_module_drain(audio_module_handle_t handle, uint32_t timeout_ms) {
    audio_module_err_t ret = audio_module_play_finish(handle);
    if (ret == AUDIO_MODULE_ERR_NOT_INIT) {
        return AUDIO_MODULE_OK;   /* 无流进行=无需等待 */
    }
    if (ret != AUDIO_MODULE_OK) {
        return ret;
    }

    uint32_t waited = 0;
    while (waited < timeout_ms) {
        bool idle = false;
        if (audio_lock_take(handle->lock, 100)) {
            if (!handle->feed_running && ring_used(handle) > 0 && !handle->abort_pending) {
                feed_start_locked(handle);   /* 自愈：喂数任务启动失败的兜底 */
            }
            idle = !handle->feed_running && (ring_used(handle) == 0);
            audio_lock_give(handle->lock);
        } else {
            idle = false;
        }
        if (idle) {
            return AUDIO_MODULE_OK;
        }
        audio_sleep_ms(5);
        waited += 5;
    }
    LOGW(TAG, "drain timeout (%ums)", (unsigned)timeout_ms);
    return AUDIO_MODULE_ERR_TIMEOUT;
}

audio_module_err_t audio_module_abort_playback(audio_module_handle_t handle) {
    if (!handle) {
        return AUDIO_MODULE_ERR_PARAM;
    }
    if (!handle->initialized || !handle->lock) {
        return AUDIO_MODULE_ERR_NOT_INIT;
    }

    bool fire_now = false;
    audio_module_play_done_cb_t cb = NULL;
    void* cb_arg = NULL;

    if (!audio_lock_take(handle->lock, 100)) {
        return AUDIO_MODULE_ERR_BUSY;
    }
    if (handle->playing_stream) {
        if (handle->feed_running) {
            handle->abort_pending = true;   /* 喂数任务下一 tick 收尾 */
        } else {
            /* 喂数任务已停（如上次启动失败）：直接收尾 */
            handle->finish_pending = false;
            handle->abort_pending = false;
            handle->playing_stream = false;
            cb = handle->done_cb;
            cb_arg = handle->done_cb_arg;
            fire_now = true;
        }
    }
    audio_lock_give(handle->lock);

    if (fire_now && cb) {
        cb(AUDIO_MODULE_STREAM_ABORTED, handle->total_written, cb_arg);
    }
    return AUDIO_MODULE_OK;
}

audio_module_err_t audio_module_set_play_done_cb(audio_module_handle_t handle,
                                                 audio_module_play_done_cb_t cb,
                                                 void* user_data) {
    if (!handle || !handle->initialized || !handle->lock) {
        return AUDIO_MODULE_ERR_NOT_INIT;
    }
    if (!audio_lock_take(handle->lock, 100)) {
        return AUDIO_MODULE_ERR_BUSY;
    }
    handle->done_cb = cb;
    handle->done_cb_arg = user_data;
    audio_lock_give(handle->lock);
    return AUDIO_MODULE_OK;
}

audio_module_err_t audio_module_set_volume(audio_module_handle_t handle, uint8_t volume) {
    if (!handle || !handle->initialized || !handle->lock) {
        return AUDIO_MODULE_ERR_NOT_INIT;
    }
    if (!audio_lock_take(handle->lock, 100)) {
        return AUDIO_MODULE_ERR_BUSY;
    }
    handle->volume = volume > 100 ? 100 : volume;
    audio_lock_give(handle->lock);
    LOGI(TAG, "volume -> %u", handle->volume);
    return AUDIO_MODULE_OK;
}

audio_module_err_t audio_module_get_volume(audio_module_handle_t handle, uint8_t* out_volume) {
    if (!handle || !out_volume || !handle->initialized || !handle->lock) {
        return AUDIO_MODULE_ERR_PARAM;
    }
    if (!audio_lock_take(handle->lock, 100)) {
        return AUDIO_MODULE_ERR_BUSY;
    }
    *out_volume = handle->volume;
    audio_lock_give(handle->lock);
    return AUDIO_MODULE_OK;
}

audio_module_err_t audio_module_mute(audio_module_handle_t handle, bool mute) {
    if (!handle || !handle->initialized || !handle->lock) {
        return AUDIO_MODULE_ERR_NOT_INIT;
    }
    if (!audio_lock_take(handle->lock, 100)) {
        return AUDIO_MODULE_ERR_BUSY;
    }
    handle->muted = mute;
    audio_lock_give(handle->lock);

    /* SD 脚硬静音：mute=关断(0)，unmute=使能(1)；未接脚时仅软件静音 */
    if (handle->sd_pin >= 0) {
        if (!audio_port_sd_write(handle->sd_pin, !mute)) {
            LOGW(TAG, "SD pin write failed (mute=%d)", mute);
        }
    }
    LOGI(TAG, "mute -> %d", mute);
    return AUDIO_MODULE_OK;
}

audio_module_err_t audio_module_is_muted(audio_module_handle_t handle, bool* out_muted) {
    if (!handle || !out_muted || !handle->initialized || !handle->lock) {
        return AUDIO_MODULE_ERR_PARAM;
    }
    if (!audio_lock_take(handle->lock, 100)) {
        return AUDIO_MODULE_ERR_BUSY;
    }
    *out_muted = handle->muted;
    audio_lock_give(handle->lock);
    return AUDIO_MODULE_OK;
}

audio_module_err_t audio_module_stop_playback(audio_module_handle_t handle) {
    if (!handle || !handle->initialized) {
        return AUDIO_MODULE_ERR_NOT_INIT;
    }

    /* 阻塞版通道：仅清标志（I2S 无直接停止 API）；异步流走 abort_playback */
    handle->playing = false;

    LOGI(TAG, "Playback stopped");

    return AUDIO_MODULE_OK;
}
