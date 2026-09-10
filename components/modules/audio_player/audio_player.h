/**
 * @file audio_player.h
 * @brief 音频播放服务（播放器策略层，≈userspace player）
 *
 * 职责（idle_modules_plan §2.2）：播放队列（设备树 queue_depth，深度≥3）、
 * 文件播放（ovs_vfs 挂载路径，裸 PCM 头）、audio_decoder_t 解码挂点
 * （v1 注册 passthrough，未来 codec2 即插）、音量 NVS 持久化。
 * 对外只暴露 EVENT_AUDIO_PLAY_* 事件（I8 契约），调用方不感知内部队列。
 *
 * 分层：本模块仅调 audio_module（PCM 设备抽象），不触 I2S 硬件。
 */

#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    AUDIO_PLAYER_OK = 0,
    AUDIO_PLAYER_ERR_PARAM = -1,
    AUDIO_PLAYER_ERR_NOT_INIT = -2,
    AUDIO_PLAYER_ERR_NO_MEM = -3,
    AUDIO_PLAYER_ERR_QUEUE_FULL = -4,    /**< 队列已满（深度=设备树 queue_depth） */
    AUDIO_PLAYER_ERR_IO = -5,            /**< 文件打开/读取失败 */
    AUDIO_PLAYER_ERR_FORMAT = -6,        /**< 无识别的解码器/头不合法 */
    AUDIO_PLAYER_ERR_NVS = -7,           /**< 音量持久化读写失败（不阻塞播放） */
    AUDIO_PLAYER_ERR_NOT_FOUND = -8,     /**< token 不存在 */
    /* 事件载荷 err 值：被停止/中止的项在 EVENT_AUDIO_PLAY_FAILED 中携带 */
    AUDIO_PLAYER_ERR_STOPPED = -100,
} audio_player_err_t;

/* ========== 裸 PCM 文件头 ========== */

/** 文件魔数："OPCM"（OVS raw PCM） */
#define AUDIO_PLAYER_PCM_MAGIC  "OPCM"

/**
 * @brief 裸 PCM 头（12 字节，无填充，小端字段）
 *
 * 文件/内存播放统一格式：[audio_player_pcm_hdr_t][data_len 字节 PCM]
 */
typedef struct {
    char     magic[4];      /**< "OPCM" */
    uint16_t rate;          /**< 采样率 Hz（如 16000） */
    uint8_t  channels;      /**< 通道数（1=mono，2=stereo） */
    uint8_t  bits;          /**< 位宽（16） */
    uint32_t data_len;      /**< 头之后的 PCM 字节数 */
} audio_player_pcm_hdr_t;

/* ========== 解码挂点 ========== */

/** 解码探测输出：PCM 流参数 */
typedef struct {
    uint16_t rate;
    uint8_t  channels;
    uint8_t  bits;
    size_t   header_len;    /**< 头部长度（数据从 data+header_len 开始） */
    size_t   data_len;      /**< 数据总字节数 */
} audio_player_pcm_info_t;

/**
 * @brief 解码器接口（codec2 未来以独立小组件注入，不改 player 主体）
 *
 * probe:  识别输入流头部，0=识别成功并填 info，非 0=不识别
 * decode: 就地流式解码；0=成功，非 0=解码失败
 * ctx:    decode 的解码器上下文（passthrough 可忽略）
 */
typedef struct {
    const char* name;
    int (*probe)(const uint8_t* data, size_t size, audio_player_pcm_info_t* info);
    int (*decode)(void* ctx, const uint8_t* in, size_t in_size,
                  uint8_t* out, size_t out_cap, size_t* out_size);
} audio_decoder_t;

/* ========== token 与状态查询 ========== */

typedef uint32_t audio_player_token_t;   /**< 0=无效 */

/** 播放项状态 */
typedef enum {
    AUDIO_PLAYER_STATE_QUEUED = 0,   /**< 在队列中等待 */
    AUDIO_PLAYER_STATE_PLAYING,      /**< 播放中 */
    AUDIO_PLAYER_STATE_IDLE,         /**< 未找到/已结束 */
} audio_player_state_t;

typedef struct {
    audio_player_state_t state;
    uint32_t position_ms;    /**< 已播位置（按样本数推算） */
    uint32_t duration_ms;    /**< 总时长（按样本数推算） */
} audio_player_status_t;

/* ========== 音量持久化移植点（audio_player_port.h 实现） ========== */

/** 读出持久化音量（无存储返回非 0） */
int apl_port_volume_load(uint8_t* out_volume);
/** 持久化音量（失败返回非 0，不阻塞播放） */
int apl_port_volume_save(uint8_t volume);

/* ========== 公共 API ========== */

/**
 * @brief 初始化播放服务（幂等；内部初始化 audio_module 与 passthrough 解码器）
 *
 * 设备树读取：audio 节点 queue_depth/volume_default（缺省 4/80）。
 * 音量持久化经 apl_port_volume_load/save 移植点（ESP=NVS / PC=不可用）。
 */
audio_player_err_t audio_player_init(void);

/**
 * @brief 反初始化（停止播放并清空队列；audio_module 为共享资源不在此释放）
 */
audio_player_err_t audio_player_deinit(void);

/**
 * @brief 追加文件播放（ovs_vfs 挂载路径，如 "/audio/notify.pcm"）
 *
 * 队列非空时追加排队；返回 token 供查询/停止。文件在播放期间由服务持有。
 */
audio_player_err_t audio_player_play_file(const char* path, audio_player_token_t* token_out);

/**
 * @brief 追加内存播放（头+PCM 同文件格式；缓冲须在 DONE/FAILED 事件前保持有效）
 */
audio_player_err_t audio_player_play_mem(const void* pcm, size_t size,
                                         audio_player_token_t* token_out);

/**
 * @brief 停止：token=当前项 → 中止当前项并继续队列；token=0 → 全停+清空队列
 *
 * 被停项发布 EVENT_AUDIO_PLAY_FAILED（err=停止原因），后续项自动接续。
 */
audio_player_err_t audio_player_stop(audio_player_token_t token);

/**
 * @brief 音量 0~100（经 audio_module 缩放；同步持久化）
 */
audio_player_err_t audio_player_set_volume(uint8_t volume);
audio_player_err_t audio_player_get_volume(uint8_t* out_volume);

/**
 * @brief 静音（透传 audio_module：SD 脚+软件双静音）
 */
audio_player_err_t audio_player_mute(bool mute);

/**
 * @brief 按 token 查询状态（token=0 查当前播放项）
 */
audio_player_err_t audio_player_get_status(audio_player_token_t token,
                                           audio_player_status_t* out);

/**
 * @brief 注册解码器（probe 命中者优先级高于已注册者；上限 4 个）
 */
audio_player_err_t audio_player_register_decoder(const audio_decoder_t* dec);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PLAYER_H */
