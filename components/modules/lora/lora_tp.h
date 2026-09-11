/**
 * @file lora_tp.h
 * @brief LoRa 传输服务层（[HUB] 批次③，契约 I7 域内服务）
 *
 * API 与语义按 docs/lora_transport_design.md §2/§3 冻结。
 * 载荷完全透明；RELIABLE=分片+滑窗 ARQ+CRC32+断点续传；
 * UNRELIABLE=单帧直发（广播仅限此 QoS）。
 * 对 UI/后台只暴露 EVENT_LORA_TP_* 事件（event_bus），不要求 include 本头。
 *
 * 可移植性硬约束：本头禁止 esp/FreeRTOS/lvgl 头；依赖经 lora_tp_deps_t
 * 注入；定时由 tasker 周期任务驱动 lora_tp_tick()，组件内不建任务。
 */

#ifndef LORA_TP_H
#define LORA_TP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 错误码（沿用项目负值惯例） ===== */
typedef enum {
    LORA_TP_OK = 0,
    LORA_TP_ERR_PARAM     = -1,
    LORA_TP_ERR_NOT_INIT  = -2,
    LORA_TP_ERR_BUSY      = -3,   /* 发送队列满 */
    LORA_TP_ERR_NO_MEM    = -4,
    LORA_TP_ERR_IO        = -5,   /* 数据源读失败 */
    LORA_TP_ERR_TIMEOUT   = -6,   /* 对端无响应（已重试耗尽） */
    LORA_TP_ERR_PEER      = -7,   /* 对端拒绝/收端落盘失败 */
    LORA_TP_ERR_CANCELLED = -8,
    LORA_TP_ERR_TOO_LARGE = -9,   /* 超过单消息上限或收端无法接收 */
} lora_tp_err_t;

/* ===== QoS 与目的对象 ===== */
typedef enum {
    LORA_TP_QOS_RELIABLE   = 0,   /* 分片+ARQ+CRC32+续传（默认） */
    LORA_TP_QOS_UNRELIABLE = 1,   /* 单帧直发，尽力而为（语音流/广播） */
} lora_tp_qos_t;

#define LORA_TP_ADDR_BCAST  0xFFFFu

typedef enum {
    LORA_TP_PROFILE_NORMAL = 0,   /* 中档：常规文本 */
    LORA_TP_PROFILE_FAST   = 1,   /* 高档：语音/文件（档位映射待手册核实） */
    LORA_TP_PROFILE_SLOW   = 2,   /* 低档：远距短文本 */
} lora_tp_profile_t;

typedef struct {
    uint16_t          addr;       /* 目标地址；0xFFFF 广播（仅 UNRELIABLE） */
    lora_tp_qos_t     qos;
    lora_tp_profile_t profile;
} lora_tp_des_t;

/* ===== 数据源/数据汇抽象（内存与文件统一，依赖注入核心） ===== */
typedef struct {
    void*         ctx;
    /* 从 offset 读 len 字节；*out 为实际读取数。由调用方实现并保证生存期 */
    lora_tp_err_t (*read)(void* ctx, uint32_t offset,
                          void* buf, size_t len, size_t* out);
} lora_tp_source_t;

typedef struct {
    void*         ctx;
    /* 把 len 字节写到 offset（收端重组落盘：RAM 环或 ovs_vfs 文件） */
    lora_tp_err_t (*write)(void* ctx, uint32_t offset,
                           const void* buf, size_t len);
} lora_tp_sink_t;

/* ===== 下层驱动接口（v1 即 lora.h 薄包装；测试用 mock 回环） ===== */
typedef struct lora_tp_deps lora_tp_deps_t;

typedef struct {
    void* drv_ctx;
    lora_tp_err_t (*send)(void* drv_ctx, const void* data, size_t len);
    lora_tp_err_t (*register_rx)(void* drv_ctx,
                                 void (*cb)(const uint8_t* data, size_t len,
                                            int8_t rssi, void* user),
                                 void* user);
} lora_drv_api_t;

/* ===== 发送 ===== */
typedef uint32_t lora_tp_token_t;   /* 会话令牌，>0 有效 */

/**
 * @brief  发送一段任意数据（异步，立即返回）
 * @param  des    目的与 QoS
 * @param  src    数据源（内存/文件由 source 抽象统一）
 * @param  total  总字节数；RELIABLE 上限 LORA_TP_MSG_MAX(默认 64KB)，
 *                UNRELIABLE 上限单帧载荷 200B
 * @param  token  出参：会话令牌，用于查询/取消
 * @note   非阻塞：数据在发送过程中必须保持可读（source 生存期覆盖整个会话）
 */
lora_tp_err_t lora_tp_send(const lora_tp_des_t* des,
                           const lora_tp_source_t* src, uint32_t total,
                           lora_tp_token_t* token);

lora_tp_err_t lora_tp_cancel(lora_tp_token_t token);

/* 会话进度查询（0~1000 千分比；状态：进行中/完成/失败及失败原因） */
typedef enum {
    LORA_TP_ST_RUNNING, LORA_TP_ST_DONE,
    LORA_TP_ST_FAILED,  LORA_TP_ST_CANCELLED,
} lora_tp_state_t;
lora_tp_err_t lora_tp_query(lora_tp_token_t token,
                            lora_tp_state_t* st, uint32_t* permille);

/* ===== 接收 ===== */
typedef struct {
    uint16_t        src;        /* 对端地址 */
    uint16_t        dst;        /* 本机或广播 */
    uint32_t        msg_id;
    uint32_t        len;
    lora_tp_qos_t   qos;
    const void*     data;       /* 非NULL：小消息连续缓冲，回调返回后失效 */
} lora_tp_rx_info_t;

/**
 * @brief 注册收包通知（收端重组完成、整条校验通过后回调一次）
 * @note  大消息（> LORA_TP_RX_INLINE_MAX，默认 2KB）data 为 NULL，
 *        应用须事先 lora_tp_set_rx_sink() 提供落盘位置，否则拒收
 *        （对端收到 ERR_PEER 而失败，不会白传）
 */
typedef void (*lora_tp_rx_cb_t)(const lora_tp_rx_info_t* info, void* user);
lora_tp_err_t lora_tp_register_rx(lora_tp_rx_cb_t cb, void* user);
lora_tp_err_t lora_tp_set_rx_sink(const lora_tp_sink_t* sink); /* NULL=恢复内建RAM */

/* ===== 生命周期与运维 ===== */

/** 依赖注入结构（design §4；tick/delay/mem 必填，drv 指向下层驱动） */
struct lora_tp_deps {
    const lora_drv_api_t* drv;          /* 下层驱动接口（v1 即 lora.h 包装） */
    uint32_t (*tick_ms)(void);          /* 平台时钟 */
    void     (*delay_ms)(uint32_t);     /* 平台延时 */
    void*    (*mem_alloc)(size_t);      /* 内存池接口（项目已建池） */
    void     (*mem_free)(void*);
};

lora_tp_err_t lora_tp_init(const lora_tp_deps_t* deps); /* 见设计 §4 */
lora_tp_err_t lora_tp_deinit(void);

/** 周期驱动（10ms，tasker Little 注册；内部不建任务） */
void lora_tp_tick(void);

typedef struct {
    uint32_t tx_ok, tx_fail, rx_ok, rx_dup, rx_crc_err;
    uint32_t frames_tx, frames_retx;
} lora_tp_stats_t;
lora_tp_err_t lora_tp_get_stats(lora_tp_stats_t* s);

#ifdef __cplusplus
}
#endif

#endif /* LORA_TP_H */
