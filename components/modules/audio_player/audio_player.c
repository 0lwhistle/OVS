/**
 * @file audio_player.c
 * @brief 音频播放服务实现（播放器策略层）
 *
 * 并发模型：pump/喂块/模块完成回调/stop 全部经 tasker Little 级执行，
 * 而该级为单 worker 线程（task_worker.c 每级一个 pthread），状态迁移天然
 * 串行化，故本文件不设独立锁；get_status 允许其他线程只读竞取（字段
 * 对齐读，容忍一拍滞后）。
 *
 * 事件（I8 契约）：EVENT_AUDIO_PLAY_STARTED/DONE/FAILED（载荷
 * event_audio_play_t：token+err+duration_ms），进度事件
 * EVENT_AUDIO_PLAY_PROGRESS 节流 ≥10%（每跨 10% 台阶最多一条）。
 */

#include "audio_player.h"
#include "audio_module.h"
#include "mem.h"
#include "event_bus.h"
#include "tasker.h"
#include "dtree.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static const char* TAG = "[AUDIO_PLAYER]";

/* ========================================================================== */
/*                              常量定义                                       */
/* ========================================================================== */

#define AP_QUEUE_DEPTH_DEFAULT   4      /* 设备树 queue_depth 缺省（≥3） */
#define AP_QUEUE_DEPTH_MIN       3
#define AP_QUEUE_DEPTH_MAX       8
#define AP_VOLUME_DEFAULT        80
#define AP_PATH_MAX              96
#define AP_PUMP_PERIOD_MS        20
#define AP_READ_CHUNK            1024   /* 文件/内存每次读取字节 */
#define AP_MAX_DECODERS          4

/* ========================================================================== */
/*                              内部数据结构                                   */
/* ========================================================================== */

/** 队列项 */
typedef struct {
    audio_player_token_t token;
    bool     is_file;
    char     path[AP_PATH_MAX];
    FILE*    fp;                    /* 文件播放句柄（is_file 时有效） */
    const uint8_t* mem;             /* mem 播放基址（头+PCM，调用方保证有效期） */
    size_t   mem_size;
    /* 播放中的流状态 */
    audio_player_pcm_info_t info;
    size_t   sent_raw;              /* 已送入解码器的原始字节数 */
    uint32_t written_pcm;           /* 已 write_async 的 PCM 字节数 */
    bool     header_done;           /* 头部已探测（info 有效） */
    bool     eof;                   /* 原始数据已读完 */
    int      dec_index;             /* 命中的解码器下标 */
} ap_item_t;

/** 服务状态 */
typedef enum {
    AP_STATE_IDLE = 0,     /* 无当前项 */
    AP_STATE_PLAYING,      /* 当前项喂数中 */
    AP_STATE_DRAINING,     /* 原始数据已喂完，等待模块收尾回调 */
} ap_state_t;

static struct {
    bool initialized;
    audio_module_handle_t amp;              /* 共享 PCM 设备 */
    ap_item_t queue[AP_QUEUE_DEPTH_MAX];    /* 等待队列（不含当前项） */
    int queue_len;
    int queue_depth;
    ap_item_t cur;                          /* 当前播放项 */
    ap_state_t state;
    audio_player_token_t next_token;        /* token 计数器（1 起） */
    uint32_t last_progress_pos;             /* 上次进度事件位置（10% 节流） */
    audio_decoder_t decoders[AP_MAX_DECODERS];
    int decoder_count;
    bool pump_running;
    uint8_t volume;
} s_ap;

/* ========================================================================== */
/*                              工具函数                                       */
/* ========================================================================== */

static uint32_t pcm_duration_ms(const audio_player_pcm_info_t* info) {
    uint32_t bytes_per_frame = (uint32_t)(info->bits / 8u) * info->channels;
    if (bytes_per_frame == 0 || info->rate == 0) {
        return 0;
    }
    return (uint32_t)((uint64_t)info->data_len * 1000u /
                      ((uint64_t)info->rate * bytes_per_frame));
}

static uint32_t pcm_position_ms(const ap_item_t* it) {
    uint32_t bytes_per_frame = (uint32_t)(it->info.bits / 8u) * it->info.channels;
    if (bytes_per_frame == 0 || it->info.rate == 0) {
        return 0;
    }
    return (uint32_t)((uint64_t)it->written_pcm * 1000u /
                      ((uint64_t)it->info.rate * bytes_per_frame));
}

/** 发布播放事件（载荷 event_audio_play_t 见 event_bus_types.h） */
static void ap_publish(event_type_t type, audio_player_token_t token,
                       int32_t err, uint32_t duration_ms) {
    event_audio_play_t ev = {
        .token = token,
        .err = err,
        .duration_ms = duration_ms,
        .position_ms = 0,
    };
    EVENT_BUS_PUBLISH(type, &ev);
}

static void ap_item_close_file(ap_item_t* it) {
    if (it->is_file && it->fp) {
        fclose(it->fp);
        it->fp = NULL;
    }
}

/* ========================================================================== */
/*                              passthrough 解码器（v1）                        */
/* ========================================================================== */

static int ap_pcm_probe(const uint8_t* data, size_t size, audio_player_pcm_info_t* info) {
    if (!data || size < sizeof(audio_player_pcm_hdr_t) || !info) {
        return -1;
    }
    audio_player_pcm_hdr_t hdr;
    memcpy(&hdr, data, sizeof(hdr));
    if (memcmp(hdr.magic, AUDIO_PLAYER_PCM_MAGIC, 4) != 0) {
        return -1;
    }
    if (hdr.rate == 0 || (hdr.bits != 8 && hdr.bits != 16) ||
        (hdr.channels != 1 && hdr.channels != 2)) {
        return -1;
    }
    info->rate = hdr.rate;
    info->channels = hdr.channels;
    info->bits = hdr.bits;
    info->header_len = sizeof(hdr);
    info->data_len = hdr.data_len;
    return 0;
}

static const audio_decoder_t AP_DECODER_PASSTHROUGH = {
    .name = "pcm-passthrough",
    .probe = ap_pcm_probe,
    .decode = NULL,   /* NULL=直通（原样写出） */
};

/* ========================================================================== */
/*                              内部函数                                       */
/* ========================================================================== */

/** 进度事件：每跨过一个 10% 台阶最多发布一条（节流 ≥10%） */
static void ap_publish_progress_if_due(ap_item_t* it) {
    uint32_t pos = pcm_position_ms(it);
    uint32_t dur = pcm_duration_ms(&it->info);
    if (dur == 0) {
        return;
    }
    uint32_t step = dur / 10u;
    if (step == 0) {
        step = 1;
    }
    if (pos >= s_ap.last_progress_pos + step) {
        s_ap.last_progress_pos = pos;
        event_audio_play_t ev = {
            .token = it->token,
            .err = 0,
            .duration_ms = dur,
            .position_ms = pos,
        };
        EVENT_BUS_PUBLISH(EVENT_AUDIO_PLAY_PROGRESS, &ev);
    }
}

/** 结束当前项（关闭文件/发事件/复位），仅 tasker Little 线程上下文调用 */
static void ap_finish_current(int32_t err, bool failed) {
    audio_player_token_t token = s_ap.cur.token;
    uint32_t dur = pcm_duration_ms(&s_ap.cur.info);

    ap_item_close_file(&s_ap.cur);
    ap_publish(failed ? EVENT_AUDIO_PLAY_FAILED : EVENT_AUDIO_PLAY_DONE,
               token, err, dur);

    memset(&s_ap.cur, 0, sizeof(s_ap.cur));
    s_ap.state = AP_STATE_IDLE;
    s_ap.last_progress_pos = 0;
}

/**
 * @brief 从当前项读取一块原始数据（文件或内存）
 * @return 实读字节数；0=数据尾；<0=IO 错误
 */
static int ap_read_raw(ap_item_t* it, uint8_t* buf, size_t cap) {
    if (it->is_file) {
        size_t n = fread(buf, 1, cap, it->fp);
        if (n == 0) {
            return feof(it->fp) ? 0 : -1;
        }
        return (int)n;
    }
    /* 内存播放：数据区 = mem + header_len 起，长 data_len（按 mem_size 钳位） */
    size_t data_start = it->info.header_len;
    size_t data_end = it->info.header_len + it->info.data_len;
    if (data_end > it->mem_size) {
        data_end = it->mem_size;
    }
    size_t pos = data_start + it->sent_raw;
    if (pos >= data_end) {
        return 0;
    }
    size_t n = data_end - pos;
    if (n > cap) {
        n = cap;
    }
    memcpy(buf, it->mem + pos, n);
    return (int)n;
}

/**
 * @brief 喂一块数据：读原始 → 解码 → audio_module 异步写
 * @return 0=推进/数据尾；1=模块缓冲满（下 tick 重试）；<0=失败
 */
static int ap_feed_chunk(ap_item_t* it) {
    static uint8_t raw[AP_READ_CHUNK];
    static uint8_t out[AP_READ_CHUNK];   /* passthrough 同尺寸直通 */

    int n = ap_read_raw(it, raw, sizeof(raw));
    if (n < 0) {
        LOGE(TAG, "read failed (token=%u)", (unsigned)it->token);
        return -1;
    }
    if (n == 0) {
        it->eof = true;
        return 0;
    }
    it->sent_raw += (size_t)n;

    const uint8_t* out_buf = raw;
    size_t out_size = (size_t)n;
    const audio_decoder_t* dec = &s_ap.decoders[it->dec_index];
    if (dec->decode) {
        if (dec->decode(NULL, raw, (size_t)n, out, sizeof(out), &out_size) != 0) {
            LOGE(TAG, "decode failed (decoder=%s)", dec->name);
            return -1;
        }
        out_buf = out;
    }

    audio_module_err_t err = audio_module_write_async(s_ap.amp, out_buf, out_size);
    if (err == AUDIO_MODULE_ERR_NO_SPACE) {
        it->sent_raw -= (size_t)n;   /* 回退，本块下 tick 重试 */
        return 1;
    }
    if (err != AUDIO_MODULE_OK) {
        LOGE(TAG, "write_async failed: %d", err);
        return -1;
    }
    it->written_pcm += (uint32_t)out_size;
    ap_publish_progress_if_due(it);
    return 0;
}

/** 取队列首项作为当前项并发布 STARTED（state 须为 IDLE） */
static void ap_start_next(void) {
    if (s_ap.queue_len <= 0) {
        return;
    }
    s_ap.cur = s_ap.queue[0];
    for (int i = 1; i < s_ap.queue_len; i++) {
        s_ap.queue[i - 1] = s_ap.queue[i];
    }
    s_ap.queue_len--;
    s_ap.state = AP_STATE_PLAYING;

    ap_publish(EVENT_AUDIO_PLAY_STARTED, s_ap.cur.token, 0,
               pcm_duration_ms(&s_ap.cur.info));
}

/**
 * @brief 探测当前项头部（解码器 probe 命中即定流参数）
 */
static bool ap_probe_header(void) {
    static uint8_t head_buf[256];
    int n;
    if (s_ap.cur.is_file) {
        n = (int)fread(head_buf, 1, sizeof(head_buf), s_ap.cur.fp);
        if (n <= 0) {
            return false;
        }
    } else {
        n = s_ap.cur.mem_size < sizeof(head_buf) ? (int)s_ap.cur.mem_size : (int)sizeof(head_buf);
        memcpy(head_buf, s_ap.cur.mem, (size_t)n);
    }

    audio_player_pcm_info_t info;
    int hit = -1;
    for (int i = 0; i < s_ap.decoder_count; i++) {
        if (s_ap.decoders[i].probe &&
            s_ap.decoders[i].probe(head_buf, (size_t)n, &info) == 0) {
            hit = i;
            break;
        }
    }
    if (hit < 0) {
        LOGW(TAG, "no decoder matched (token=%u)", (unsigned)s_ap.cur.token);
        return false;
    }

    s_ap.cur.info = info;
    s_ap.cur.dec_index = hit;
    s_ap.cur.header_done = true;

    if (s_ap.cur.is_file) {
        /* 文件：回卷到数据区起点（头部长度处），sent_raw 从 0 计 */
        if (fseek(s_ap.cur.fp, (long)info.header_len, SEEK_SET) != 0) {
            return false;
        }
    }
    LOGI(TAG, "play start: %s rate=%u ch=%u bits=%u len=%u ms (decoder=%s)",
         s_ap.cur.is_file ? s_ap.cur.path : "<mem>",
         (unsigned)info.rate, info.channels, info.bits,
         (unsigned)pcm_duration_ms(&info),
         s_ap.decoders[hit].name ? s_ap.decoders[hit].name : "?");
    return true;
}

/**
 * @brief pump 周期任务（tasker Little 20ms）：队列推进 + 喂数
 */
static enum task_t ap_pump_tick(void* arg) {
    (void)arg;
    if (!s_ap.initialized) {
        return TASK_OK;
    }

    if (s_ap.state == AP_STATE_IDLE) {
        if (s_ap.queue_len <= 0) {
            /* 队列空且空闲：停 pump，回落零开销 */
            tasker_cancel_by_name("audio_pump");
            s_ap.pump_running = false;
            return TASK_OK;
        }
        ap_start_next();
    }

    if (s_ap.state != AP_STATE_PLAYING) {
        return TASK_OK;   /* DRAINING：等模块回调收尾 */
    }

    if (!s_ap.cur.header_done) {
        if (!ap_probe_header()) {
            ap_finish_current(AUDIO_PLAYER_ERR_FORMAT, true);
        }
        return TASK_OK;   /* 数据块下 tick 开始喂 */
    }

    int r = ap_feed_chunk(&s_ap.cur);
    if (r < 0) {
        /* 经模块 abort 统一走收尾回调（发 FAILED） */
        audio_module_abort_playback(s_ap.amp);
        return TASK_OK;
    }
    if (s_ap.cur.eof) {
        s_ap.state = AP_STATE_DRAINING;
        audio_module_play_finish(s_ap.amp);
    }
    return TASK_OK;
}

static void ap_pump_ensure_started(void) {
    if (s_ap.pump_running || !s_ap.initialized) {
        return;
    }
    struct task_node node;
    if (tasker_task_init_li(&node, AP_PUMP_PERIOD_MS, -1,
                            "audio_pump", ap_pump_tick, NULL) == TASK_OK &&
        tasker_enqueue(&node) == TASK_OK) {
        s_ap.pump_running = true;
    } else {
        LOGE(TAG, "pump task start failed");
    }
}

/**
 * @brief audio_module 流收尾回调（tasker Little 线程上下文，轻量）
 *
 * PLAYING 与 DRAINING 均需处理：abort 可能发生在喂数中（PLAYING），
 * 若忽略会使当前项滞留 PLAYING 并在下次 write_async 时复活。
 */
static void ap_on_module_done(audio_module_stream_status_t status,
                              uint32_t total_bytes, void* user_data) {
    (void)total_bytes;
    (void)user_data;
    if (!s_ap.initialized || s_ap.state == AP_STATE_IDLE) {
        return;   /* 空闲态的回调（如启动失败的兜底 abort）忽略 */
    }

    bool handled = false;
    if (status == AUDIO_MODULE_STREAM_ABORTED) {
        ap_finish_current(AUDIO_PLAYER_ERR_STOPPED, true);
        handled = true;
    } else if (status == AUDIO_MODULE_STREAM_ERROR) {
        ap_finish_current(AUDIO_PLAYER_ERR_IO, true);
        handled = true;
    } else if (status == AUDIO_MODULE_STREAM_DONE && s_ap.state == AP_STATE_DRAINING) {
        ap_finish_current(0, false);
        handled = true;
    }
    /* PLAYING 态的 DONE 属非预期（无 finish 标记），忽略 */

    if (handled && s_ap.queue_len > 0) {
        /* 队列还有项则接续（pump 未停则下一 tick 自动开始） */
        ap_pump_ensure_started();
    }
}

/* ========================================================================== */
/*                              公共API实现                                    */
/* ========================================================================== */

audio_player_err_t audio_player_init(void) {
    if (s_ap.initialized) {
        LOGW(TAG, "Already initialized");
        return AUDIO_PLAYER_OK;
    }

    memset(&s_ap, 0, sizeof(s_ap));

    /* 设备树策略键（缺省兜底） */
    s_ap.queue_depth = AP_QUEUE_DEPTH_DEFAULT;
    uint8_t vol = AP_VOLUME_DEFAULT;
    dtree_node_t* policy = dtree_find_by_compatible("ovs-audio-policy");
    if (policy) {
        int32_t v;
        if (dtree_get_int(policy, "queue_depth", &v) == DTREE_OK) {
            if (v < AP_QUEUE_DEPTH_MIN) {
                LOGW(TAG, "queue_depth %" PRId32 " < minimum %d, clamped", v, AP_QUEUE_DEPTH_MIN);
                v = AP_QUEUE_DEPTH_MIN;
            }
            if (v > AP_QUEUE_DEPTH_MAX) {
                v = AP_QUEUE_DEPTH_MAX;
            }
            s_ap.queue_depth = (int)v;
        }
        if (dtree_get_int(policy, "volume_default", &v) == DTREE_OK &&
            v >= 0 && v <= 100) {
            vol = (uint8_t)v;
        }
    }

    /* PCM 设备（audio_module 幂等共享） */
    audio_module_err_t err = audio_module_init(&s_ap.amp);
    if (err != AUDIO_MODULE_OK) {
        LOGE(TAG, "audio_module init failed: %d (player degraded)", err);
        return AUDIO_PLAYER_ERR_NOT_INIT;
    }

    /* 注册 v1 passthrough 解码器 */
    s_ap.decoders[s_ap.decoder_count++] = AP_DECODER_PASSTHROUGH;

    /* 音量：持久化值优先，缺省设备树 */
    uint8_t stored = 0;
    if (apl_port_volume_load(&stored) == 0 && stored <= 100) {
        vol = stored;
    }
    audio_module_set_volume(s_ap.amp, vol);
    s_ap.volume = vol;

    /* 流收尾回调 */
    audio_module_set_play_done_cb(s_ap.amp, ap_on_module_done, NULL);

    s_ap.initialized = true;
    LOGI(TAG, "audio_player ready (queue_depth=%d, volume=%u)", s_ap.queue_depth, vol);
    return AUDIO_PLAYER_OK;
}

audio_player_err_t audio_player_deinit(void) {
    if (!s_ap.initialized) {
        return AUDIO_PLAYER_ERR_NOT_INIT;
    }
    audio_player_stop(0);
    s_ap.initialized = false;
    tasker_cancel_by_name("audio_pump");
    s_ap.pump_running = false;
    LOGI(TAG, "audio_player deinitialized");
    return AUDIO_PLAYER_OK;
}

static audio_player_err_t ap_enqueue(bool is_file, const char* path,
                                     const void* mem, size_t mem_size,
                                     audio_player_token_t* token_out) {
    if (!s_ap.initialized) {
        return AUDIO_PLAYER_ERR_NOT_INIT;
    }
    if (is_file && (!path || !path[0])) {
        return AUDIO_PLAYER_ERR_PARAM;
    }
    if (!is_file && (!mem || mem_size < sizeof(audio_player_pcm_hdr_t))) {
        return AUDIO_PLAYER_ERR_PARAM;
    }
    if (s_ap.queue_len >= s_ap.queue_depth) {
        LOGW(TAG, "queue full (%d)", s_ap.queue_len);
        return AUDIO_PLAYER_ERR_QUEUE_FULL;
    }

    ap_item_t* it = &s_ap.queue[s_ap.queue_len++];
    memset(it, 0, sizeof(*it));
    it->token = ++s_ap.next_token;
    it->is_file = is_file;
    if (is_file) {
        strncpy(it->path, path, sizeof(it->path) - 1);
        it->fp = fopen(it->path, "rb");
        if (!it->fp) {
            LOGE(TAG, "open failed: %s", it->path);
            s_ap.queue_len--;
            return AUDIO_PLAYER_ERR_IO;
        }
    } else {
        it->mem = (const uint8_t*)mem;
        it->mem_size = mem_size;
    }

    if (token_out) {
        *token_out = it->token;
    }
    ap_pump_ensure_started();
    LOGI(TAG, "queued %s (token=%u, queue=%d)",
         is_file ? it->path : "<mem>", (unsigned)it->token, s_ap.queue_len);
    return AUDIO_PLAYER_OK;
}

audio_player_err_t audio_player_play_file(const char* path, audio_player_token_t* token_out) {
    return ap_enqueue(true, path, NULL, 0, token_out);
}

audio_player_err_t audio_player_play_mem(const void* pcm, size_t size,
                                         audio_player_token_t* token_out) {
    return ap_enqueue(false, NULL, pcm, size, token_out);
}

/* stop 的实际执行（tasker Little 单线程内与 pump/feed 串行） */
static enum task_t ap_stop_task(void* arg) {
    audio_player_token_t token = (audio_player_token_t)(uintptr_t)arg;
    if (!s_ap.initialized) {
        return TASK_OK;
    }

    if (token == 0) {
        /* 全停：清队列 + 中止当前项 */
        for (int i = 0; i < s_ap.queue_len; i++) {
            ap_publish(EVENT_AUDIO_PLAY_FAILED, s_ap.queue[i].token,
                       AUDIO_PLAYER_ERR_STOPPED, 0);
            ap_item_close_file(&s_ap.queue[i]);
        }
        s_ap.queue_len = 0;
        if (s_ap.state != AP_STATE_IDLE) {
            audio_module_abort_playback(s_ap.amp);   /* 回调内发 FAILED 并置 IDLE */
        }
        LOGI(TAG, "stop all");
        return TASK_OK;
    }

    if (s_ap.state != AP_STATE_IDLE && s_ap.cur.token == token) {
        audio_module_abort_playback(s_ap.amp);
        return TASK_OK;
    }

    for (int i = 0; i < s_ap.queue_len; i++) {
        if (s_ap.queue[i].token == token) {
            ap_publish(EVENT_AUDIO_PLAY_FAILED, token, AUDIO_PLAYER_ERR_STOPPED, 0);
            ap_item_close_file(&s_ap.queue[i]);
            for (int j = i + 1; j < s_ap.queue_len; j++) {
                s_ap.queue[j - 1] = s_ap.queue[j];
            }
            s_ap.queue_len--;
            return TASK_OK;
        }
    }

    LOGW(TAG, "stop: token %u not found", (unsigned)token);
    return TASK_OK;
}

audio_player_err_t audio_player_stop(audio_player_token_t token) {
    if (!s_ap.initialized) {
        return AUDIO_PLAYER_ERR_NOT_INIT;
    }
    /* 经 tasker 路由到 Little 线程：与 pump/回调串行，免锁 */
    struct task_node node;
    if (tasker_task_init_li(&node, 0, 1, "audio_stop", ap_stop_task,
                            (void*)(uintptr_t)token) == TASK_OK &&
        tasker_enqueue(&node) == TASK_OK) {
        return AUDIO_PLAYER_OK;
    }
    LOGE(TAG, "stop task enqueue failed");
    return AUDIO_PLAYER_ERR_NO_MEM;
}

audio_player_err_t audio_player_set_volume(uint8_t volume) {
    if (!s_ap.initialized) {
        return AUDIO_PLAYER_ERR_NOT_INIT;
    }
    if (volume > 100) {
        volume = 100;
    }
    audio_module_err_t err = audio_module_set_volume(s_ap.amp, volume);
    if (err != AUDIO_MODULE_OK) {
        return AUDIO_PLAYER_ERR_NOT_INIT;
    }
    s_ap.volume = volume;
    if (apl_port_volume_save(volume) != 0) {
        LOGW(TAG, "volume save failed (nvs?)");
    }
    return AUDIO_PLAYER_OK;
}

audio_player_err_t audio_player_get_volume(uint8_t* out_volume) {
    if (!out_volume) {
        return AUDIO_PLAYER_ERR_PARAM;
    }
    *out_volume = s_ap.volume;
    return s_ap.initialized ? AUDIO_PLAYER_OK : AUDIO_PLAYER_ERR_NOT_INIT;
}

audio_player_err_t audio_player_mute(bool mute) {
    if (!s_ap.initialized) {
        return AUDIO_PLAYER_ERR_NOT_INIT;
    }
    return audio_module_mute(s_ap.amp, mute) == AUDIO_MODULE_OK
               ? AUDIO_PLAYER_OK : AUDIO_PLAYER_ERR_NOT_INIT;
}

audio_player_err_t audio_player_get_status(audio_player_token_t token,
                                           audio_player_status_t* out) {
    if (!out) {
        return AUDIO_PLAYER_ERR_PARAM;
    }
    if (!s_ap.initialized) {
        return AUDIO_PLAYER_ERR_NOT_INIT;
    }

    if (token == 0) {
        token = s_ap.cur.token;
    }

    if (s_ap.state != AP_STATE_IDLE && s_ap.cur.token == token) {
        out->state = AUDIO_PLAYER_STATE_PLAYING;
        out->position_ms = pcm_position_ms(&s_ap.cur);
        out->duration_ms = pcm_duration_ms(&s_ap.cur.info);
        return AUDIO_PLAYER_OK;
    }
    for (int i = 0; i < s_ap.queue_len; i++) {
        if (s_ap.queue[i].token == token) {
            out->state = AUDIO_PLAYER_STATE_QUEUED;
            out->position_ms = 0;
            out->duration_ms = 0;
            return AUDIO_PLAYER_OK;
        }
    }
    out->state = AUDIO_PLAYER_STATE_IDLE;
    out->position_ms = 0;
    out->duration_ms = 0;
    return AUDIO_PLAYER_OK;
}

audio_player_err_t audio_player_register_decoder(const audio_decoder_t* dec) {
    if (!dec || !dec->name || !dec->probe) {
        return AUDIO_PLAYER_ERR_PARAM;
    }
    if (s_ap.decoder_count >= AP_MAX_DECODERS) {
        LOGW(TAG, "decoder table full");
        return AUDIO_PLAYER_ERR_NO_MEM;
    }
    for (int i = 0; i < s_ap.decoder_count; i++) {
        if (strcmp(s_ap.decoders[i].name, dec->name) == 0) {
            LOGW(TAG, "decoder '%s' already registered", dec->name);
            return AUDIO_PLAYER_ERR_PARAM;
        }
    }
    s_ap.decoders[s_ap.decoder_count++] = *dec;
    LOGI(TAG, "decoder registered: %s", dec->name);
    return AUDIO_PLAYER_OK;
}
