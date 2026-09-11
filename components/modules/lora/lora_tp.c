/**
 * @file lora_tp.c
 * @brief LoRa 传输服务层实现（design：docs/lora_transport_design.md §2/§3）
 *
 * 分层：本组件只调 lora_drv_api_t（lora.h 薄包装/mock），不碰 UART/M0/
 * M1/AUX/AT。帧格式按 lora_protocol.md §2.1（11B 帧头，小端）；DATA 载荷
 * 前置本组件消息子头 12B：msg_id(2)+offset(2)+total(4)+crc32(4)——
 * 即 §5.1 子头的扩展版（rsv 字段定为片偏移低 16 位 + 4B 消息级 CRC32）。
 *
 * 关键语义（§3）：
 *  - 异步会话制：FIFO 队列（深度可配），单 RELIABLE 会话在传（半双工）；
 *  - 滑窗 ARQ：发满一个窗口→等 DATA_ACK（累积确认，收端回最高连续
 *    offset）→从断点续读 source；超时整窗重试，max_retries 耗尽 FAILED；
 *  - 收端只按序落盘（offset==contiguous），乱序/重复片 ACK 断点不落盘，
 *    使消息级 CRC32 可增量计算（RAM 与 sink 路径统一）；
 *  - 幂等：重复片静默计数（rx_dup）；
 *  - 广播：仅 UNRELIABLE；RELIABLE 广播在 API 层拒绝（ERR_PARAM）；
 *  - 仲裁钩子：PRI 标志已解析（VOICE 帧属 v2，本期无生产者）。
 *
 * LEVEL 档位数值为手册核实前的占位值（lora_protocol.md §8）。
 */

#include "lora_tp_internal.h"
#include "dtree.h"
#include "logger.h"
#include "event_bus.h"

#include <stdio.h>
#include <string.h>

static const char* TAG = "[LORA_TP]";

/* ========================================================================== */
/*                              常量与帧格式                                    */
/* ========================================================================== */

#define LORA_TP_FRAME_MAGIC       0x4Cu
#define LORA_TP_PROTO_VER         0x01u
#define LORA_TP_FRM_DATA          0x01u
#define LORA_TP_FRM_DATA_ACK      0x05u

#define LORA_TP_FLAG_NOACK        0x02u
#define LORA_TP_FLAG_MORE         0x04u

#define LORA_TP_FRAME_HDR         11u
#define LORA_TP_FRAME_PAYLOAD_MAX 200u
#define LORA_TP_SUBHDR            12u   /* msg_id2+offset2+total4+crc4 */
#define LORA_TP_CHUNK_MAX         (LORA_TP_FRAME_PAYLOAD_MAX - LORA_TP_SUBHDR) /* 188 */

#define LORA_TP_ACK_PAYLOAD       8u    /* msg_id2+status1+pad1+contig4 */
#define LORA_TP_ACK_STATUS_OFF    2u
#define LORA_TP_ACK_CONTIG_OFF    4u
#define LORA_TP_ACK_OK            0x00u
#define LORA_TP_ACK_ERR           0x01u

#define LORA_TP_QUEUE_MAX         8u

/* ========================================================================== */
/*                              类型                                            */
/* ========================================================================== */

typedef struct {
    lora_tp_des_t des;
    lora_tp_source_t src;
    uint32_t total;
    lora_tp_token_t token;
} tx_req_t;

/** 发送会话状态（§6 状态机的传输段） */
typedef enum {
    TXS_IDLE = 0,      /* 无会话在传 */
    TXS_TRANSFER,      /* 窗口发送中 */
    TXS_WAIT_ACK,      /* 等累积 ACK（超时→整窗重试） */
} tx_state_t;

typedef struct {
    lora_tp_token_t token;
    tx_state_t state;
    lora_tp_err_t err;
    uint16_t msg_id;
    uint32_t total;
    uint32_t crc;
    uint32_t cursor;            /* 已确认连续 offset（断点） */
    uint16_t next_seq;
    int retries;                /* 整窗重试计数 */
    uint32_t next_send_ms;      /* 帧间 gap 限速 */
    uint32_t ack_deadline_ms;
    uint32_t last_progress_pm;  /* PROGRESS 事件 5% 节流 */
    uint32_t win_last_off;      /* 本窗口最后发出的片尾 offset（诊断用） */
    tx_req_t req;
} tx_sess_t;

typedef struct {
    bool active;
    uint16_t src;
    uint16_t msg_id;
    uint32_t total;
    uint32_t crc;
    uint32_t contiguous;
    uint32_t crc_state;         /* 已按序接收字节的链式 CRC32 */
    uint32_t last_total;
    lora_tp_qos_t qos;
    uint8_t* ram;               /* 非NULL：内建 RAM 重组缓冲 */
} rx_sess_t;

struct lora_tp_ctx {
    bool inited;
    lora_tp_deps_t deps;
    lora_tp_cfg_t cfg;

    tx_req_t queue[LORA_TP_QUEUE_MAX];
    uint8_t queue_len;

    tx_sess_t tx;
    rx_sess_t rx;

    /* 幂等投递：最近完成的消息（对端重传已交付消息时只回 ACK，
     * 不随 rx_reset 复位） */
    bool rx_have_last;
    uint16_t rx_last_src;
    uint16_t rx_last_msg;
    uint32_t rx_last_total;

    lora_tp_rx_cb_t rx_cb;
    void* rx_user;
    lora_tp_sink_t rx_sink;
    bool rx_sink_valid;

    lora_tp_token_t next_token;
    uint16_t next_msg_id;

    lora_tp_stats_t stats;

    /* 最近完结会话记录（query 在 DONE/FAILED 后仍可读） */
    struct {
        lora_tp_token_t token;
        lora_tp_state_t st;
        uint32_t total;
    } last_done;
};

/* 公共 API 单例 */
static lora_tp_ctx_t* s_inst = NULL;
static uint8_t s_inst_mem[sizeof(struct lora_tp_ctx)];

/* ========================================================================== */
/*                              小工具                                          */
/* ========================================================================== */

/* CRC-32/IEEE（无表实现，消息级端到端校验） */
static uint32_t crc32_calc(uint32_t crc, const uint8_t* data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }
    return ~crc;
}

static void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)(v >> 24);
}
static uint16_t get16(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t now_ms(lora_tp_ctx_t* c) {
    return c->deps.tick_ms ? c->deps.tick_ms() : 0;
}

static uint32_t prof_ack_timeout(const lora_tp_cfg_t* cfg, lora_tp_profile_t p) {
    int i = (p > LORA_TP_PROFILE_SLOW) ? 0 : (int)p;
    return cfg->ack_timeout_ms[i];
}
static uint32_t prof_tx_gap(const lora_tp_cfg_t* cfg, lora_tp_profile_t p) {
    int i = (p > LORA_TP_PROFILE_SLOW) ? 0 : (int)p;
    return cfg->tx_gap_ms[i];
}

static void record_done(lora_tp_ctx_t* c, lora_tp_token_t token,
                        lora_tp_state_t st, uint32_t total) {
    c->last_done.token = token;
    c->last_done.st = st;
    c->last_done.total = total;
}

static void emit_event(event_type_t ev, lora_tp_token_t token, uint16_t peer,
                       uint32_t len, int32_t err, uint32_t permille) {
    event_lora_tp_t p = {
        .token = token, .peer = peer, .len = len,
        .err = (int)err, .permille = permille,
    };
    event_bus_publish(ev, &p, sizeof(p));
}

/* ========================================================================== */
/*                          配置（设备树 + 缺省兜底）                           */
/* ========================================================================== */

void lora_tp_cfg_default(lora_tp_cfg_t* cfg) {
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->local_addr = 0x0001;
    cfg->tx_queue_depth = 4;
    cfg->window = 4;
    cfg->max_retries = 3;
    cfg->msg_max_bytes = 65536;
    cfg->rx_inline_max_bytes = 2048;
    /* LEVEL 档位占位值（手册核实前，勿当最终参数） */
    cfg->ack_timeout_ms[0] = 350;   cfg->tx_gap_ms[0] = 300;    /* NORMAL */
    cfg->ack_timeout_ms[1] = 110;   cfg->tx_gap_ms[1] = 60;     /* FAST */
    cfg->ack_timeout_ms[2] = 5700;  cfg->tx_gap_ms[2] = 5600;   /* SLOW */
}

/* 设备树：compatible="lora-tp"；无节点时全部走缺省（§5 兜底） */
static void load_cfg_from_dtree(lora_tp_cfg_t* cfg) {
    dtree_node_t* node = dtree_find_by_compatible("lora-tp");
    if (!node) {
        return;
    }
    int32_t v = 0;
    if (dtree_get_int(node, "local_address", &v) == DTREE_OK && v > 0 && v <= 0xFFFE) {
        cfg->local_addr = (uint16_t)v;
    }
    if (dtree_get_int(node, "tx_queue_depth", &v) == DTREE_OK &&
        v >= 1 && v <= (int32_t)LORA_TP_QUEUE_MAX) {
        cfg->tx_queue_depth = (uint8_t)v;
    }
    if (dtree_get_int(node, "window", &v) == DTREE_OK && v >= 1 && v <= 8) {
        cfg->window = (uint8_t)v;
    }
    if (dtree_get_int(node, "max_retries", &v) == DTREE_OK && v >= 1 && v <= 10) {
        cfg->max_retries = (uint8_t)v;
    }
    if (dtree_get_int(node, "msg_max_bytes", &v) == DTREE_OK && v >= 200 && v <= 65535) {
        cfg->msg_max_bytes = (uint32_t)v;
    }
    if (dtree_get_int(node, "rx_inline_max_bytes", &v) == DTREE_OK && v >= 200) {
        cfg->rx_inline_max_bytes = (uint32_t)v;
    }
    for (int i = 0; i < 3; i++) {
        char key[32];
        snprintf(key, sizeof(key), "ack_timeout_ms_p%d", i);
        if (dtree_get_int(node, key, &v) == DTREE_OK && v >= 10) {
            cfg->ack_timeout_ms[i] = (uint32_t)v;
        }
        snprintf(key, sizeof(key), "tx_gap_ms_p%d", i);
        if (dtree_get_int(node, key, &v) == DTREE_OK && v >= 10) {
            cfg->tx_gap_ms[i] = (uint32_t)v;
        }
    }
}

/* ========================================================================== */
/*                              发送路径                                        */
/* ========================================================================== */

static lora_tp_err_t send_frame(lora_tp_ctx_t* c, const uint8_t* frame, size_t len) {
    if (!c->deps.drv || !c->deps.drv->send) {
        return LORA_TP_ERR_NOT_INIT;
    }
    lora_tp_err_t err = c->deps.drv->send(c->deps.drv->drv_ctx, frame, len);
    if (err == LORA_TP_OK) {
        c->stats.frames_tx++;
    }
    return err;
}

/** 组 DATA 帧并发送（载荷=子头12B+chunk） */
static lora_tp_err_t send_data_frame(lora_tp_ctx_t* c, tx_sess_t* s,
                                     uint32_t offset, const uint8_t* data,
                                     size_t chunk, bool more) {
    uint8_t frame[LORA_TP_FRAME_HDR + LORA_TP_FRAME_PAYLOAD_MAX];
    uint8_t* p = frame;
    p[0] = LORA_TP_FRAME_MAGIC;
    p[1] = LORA_TP_PROTO_VER;
    p[2] = LORA_TP_FRM_DATA;
    p[3] = (s->req.des.qos == LORA_TP_QOS_UNRELIABLE) ? LORA_TP_FLAG_NOACK
          : (more ? LORA_TP_FLAG_MORE : 0);
    put16(p + 4, s->req.des.addr);
    put16(p + 6, c->cfg.local_addr);
    put16(p + 8, s->next_seq++);
    p[10] = (uint8_t)(LORA_TP_SUBHDR + chunk);

    uint8_t* q = p + LORA_TP_FRAME_HDR;
    put16(q, s->msg_id);
    put16(q + 2, (uint16_t)offset);   /* 片偏移（≤64KB 消息内） */
    put32(q + 4, s->total);
    put32(q + 8, s->crc);
    memcpy(q + LORA_TP_SUBHDR, data, chunk);

    return send_frame(c, frame, LORA_TP_FRAME_HDR + LORA_TP_SUBHDR + chunk);
}

/** 全消息预读一遍算 CRC32（同时提前暴露 source IO 错误） */
static lora_tp_err_t compute_msg_crc(lora_tp_ctx_t* c, tx_sess_t* s) {
    uint8_t buf[LORA_TP_CHUNK_MAX];
    uint32_t off = 0;
    uint32_t crc = 0;
    while (off < s->total) {
        size_t want = s->total - off;
        if (want > LORA_TP_CHUNK_MAX) {
            want = LORA_TP_CHUNK_MAX;
        }
        size_t got = 0;
        lora_tp_err_t err = s->req.src.read(s->req.src.ctx, off, buf, want, &got);
        if (err != LORA_TP_OK || got == 0) {
            LOGW(TAG, "crc pre-read failed at %u: err=%d got=%u", (unsigned)off, err, (unsigned)got);
            return (err != LORA_TP_OK) ? err : LORA_TP_ERR_IO;
        }
        crc = crc32_calc(crc, buf, got);
        off += (uint32_t)got;
    }
    (void)c;
    s->crc = crc;
    return LORA_TP_OK;
}

/* ========================================================================== */
/*                              会话状态机                                      */
/* ========================================================================== */

static void tx_finish(lora_tp_ctx_t* c, tx_sess_t* s,
                      lora_tp_state_t st, lora_tp_err_t err) {
    if (st == LORA_TP_ST_DONE) {
        LOGI(TAG, "tx done: token=%u total=%u", (unsigned)s->token, (unsigned)s->total);
        c->stats.tx_ok++;
        emit_event(EVENT_LORA_TP_TX_DONE, s->token, s->req.des.addr,
                   s->total, LORA_TP_OK, 1000);
    } else if (st == LORA_TP_ST_CANCELLED) {
        LOGI(TAG, "tx cancelled: token=%u", (unsigned)s->token);
        emit_event(EVENT_LORA_TP_TX_FAILED, s->token, s->req.des.addr,
                   s->total, LORA_TP_ERR_CANCELLED, 0);
    } else {
        LOGW(TAG, "tx failed: token=%u err=%d retries=%d", (unsigned)s->token, err, s->retries);
        c->stats.tx_fail++;
        emit_event(EVENT_LORA_TP_TX_FAILED, s->token, s->req.des.addr,
                   s->total, err, 0);
    }
    record_done(c, s->token, st, s->total);
    s->state = TXS_IDLE;
    s->err = err;
}

/** 从队列取下一个请求开始会话（IDLE 态调用） */
static void start_queued(lora_tp_ctx_t* c) {
    if (c->tx.state != TXS_IDLE || c->queue_len == 0) {
        return;
    }
    tx_req_t req = c->queue[0];
    memmove(&c->queue[0], &c->queue[1],
            sizeof(tx_req_t) * (size_t)(c->queue_len - 1));
    c->queue_len--;

    tx_sess_t* s = &c->tx;
    memset(s, 0, sizeof(*s));
    s->req = req;
    s->token = req.token;
    s->total = req.total;
    s->msg_id = c->next_msg_id++;
    s->next_seq = 1;
    s->next_send_ms = now_ms(c);

    if (req.des.qos == LORA_TP_QOS_UNRELIABLE) {
        /* 单帧直发（NOACK）：成功即 DONE */
        uint8_t buf[LORA_TP_CHUNK_MAX];
        size_t got = 0;
        lora_tp_err_t err = req.src.read(req.src.ctx, 0, buf, LORA_TP_CHUNK_MAX, &got);
        if (err != LORA_TP_OK || got == 0) {
            LOGW(TAG, "unreliable src read failed: err=%d got=%u", err, (unsigned)got);
            tx_finish(c, s, LORA_TP_ST_FAILED, LORA_TP_ERR_IO);
            return;
        }
        {
            /* CRC 覆盖实际发出的数据 */
            uint32_t crc = crc32_calc(0, buf, got);
            s->crc = crc;
        }
        if (send_data_frame(c, s, 0, buf, got, false) != LORA_TP_OK) {
            tx_finish(c, s, LORA_TP_ST_FAILED, LORA_TP_ERR_IO);
            return;
        }
        LOGI(TAG, "unreliable sent: token=%u addr=0x%04X len=%u",
             (unsigned)req.token, req.des.addr, (unsigned)got);
        tx_finish(c, s, LORA_TP_ST_DONE, LORA_TP_OK);
        return;
    }

    /* RELIABLE：预读算 CRC，失败即 FAILED(IO) 不上线 */
    lora_tp_err_t err = compute_msg_crc(c, s);
    if (err != LORA_TP_OK) {
        tx_finish(c, s, LORA_TP_ST_FAILED, err);
        return;
    }
    LOGI(TAG, "reliable start: token=%u addr=0x%04X total=%u msg_id=%u",
         (unsigned)req.token, req.des.addr, (unsigned)req.total, s->msg_id);
    s->state = TXS_TRANSFER;
}

/** TRANSFER 态推进：发一个窗口（受 gap 限速），发完转 WAIT_ACK */
static void window_advance(lora_tp_ctx_t* c) {
    tx_sess_t* s = &c->tx;
    uint8_t buf[LORA_TP_CHUNK_MAX];
    uint32_t t = now_ms(c);

    s->win_last_off = 0;
    uint32_t off = s->cursor;
    int sent = 0;
    for (int i = 0; i < c->cfg.window; i++) {
        if (off >= s->total || t < s->next_send_ms) {
            break;
        }
        size_t want = s->total - off;
        if (want > LORA_TP_CHUNK_MAX) {
            want = LORA_TP_CHUNK_MAX;
        }
        size_t got = 0;
        lora_tp_err_t err = s->req.src.read(s->req.src.ctx, off, buf, want, &got);
        if (err != LORA_TP_OK || got == 0) {
            LOGW(TAG, "source read failed at %u: err=%d got=%u", (unsigned)off, err, (unsigned)got);
            tx_finish(c, s, LORA_TP_ST_FAILED, LORA_TP_ERR_IO);
            return;
        }
        if (send_data_frame(c, s, off, buf, got, off + got < s->total) != LORA_TP_OK) {
            LOGW(TAG, "drv busy, frame deferred");
            break;   /* 下个 tick 重试 */
        }
        if (s->state == TXS_IDLE) {
            /* 同步回环下，本帧触发的 ACK 可能已把会话收尾（DONE/FAILED） */
            return;
        }
        s->win_last_off = off + (uint32_t)got;
        off += (uint32_t)got;
        sent++;
        s->next_send_ms = t + prof_tx_gap(&c->cfg, s->req.des.profile);
    }

    if (sent == 0) {
        return;   /* gap 限速中（off>=total 且 cursor 未到位时等 ACK） */
    }
    (void)s->win_last_off;
    if (s->state == TXS_TRANSFER) {   /* 同步 ACK 可能已收尾会话 */
        s->state = TXS_WAIT_ACK;
        s->ack_deadline_ms = now_ms(c) + prof_ack_timeout(&c->cfg, s->req.des.profile);
    }
}

/** WAIT_ACK 超时：整窗重试（从 cursor 断点续发），耗尽则 FAILED */
static void ack_timeout(lora_tp_ctx_t* c) {
    tx_sess_t* s = &c->tx;
    s->retries++;
    if (s->retries > c->cfg.max_retries) {
        LOGW(TAG, "ack timeout exhausted, give up at %u/%u", (unsigned)s->cursor, (unsigned)s->total);
        tx_finish(c, s, LORA_TP_ST_FAILED, LORA_TP_ERR_TIMEOUT);
        return;
    }
    LOGW(TAG, "ack timeout, window retry #%d from offset %u", s->retries, (unsigned)s->cursor);
    c->stats.frames_retx++;
    s->state = TXS_TRANSFER;
    s->next_send_ms = now_ms(c);   /* §5.2 退避简化为下一 tick */
}

void lora_tp_inst_tick(lora_tp_ctx_t* c) {
    if (!c || !c->inited) {
        return;
    }
    tx_sess_t* s = &c->tx;
    if (s->state == TXS_IDLE) {
        start_queued(c);
        if (s->state == TXS_TRANSFER) {
            window_advance(c);   /* 首窗口立即推进 */
        }
    } else if (s->state == TXS_TRANSFER) {
        window_advance(c);
    } else if (s->state == TXS_WAIT_ACK) {
        if (now_ms(c) >= s->ack_deadline_ms) {
            ack_timeout(c);
        }
    }
}

/* ========================================================================== */
/*                              接收路径                                        */
/* ========================================================================== */

static void rx_send_ack(lora_tp_ctx_t* c, uint16_t dst, uint16_t msg_id,
                        uint32_t contiguous, uint8_t status) {
    uint8_t frame[LORA_TP_FRAME_HDR + LORA_TP_ACK_PAYLOAD];
    uint8_t* p = frame;
    p[0] = LORA_TP_FRAME_MAGIC;
    p[1] = LORA_TP_PROTO_VER;
    p[2] = LORA_TP_FRM_DATA_ACK;
    p[3] = 0;
    put16(p + 4, dst);
    put16(p + 6, c->cfg.local_addr);
    put16(p + 8, 0);
    p[10] = LORA_TP_ACK_PAYLOAD;
    uint8_t* q = p + LORA_TP_FRAME_HDR;
    put16(q, msg_id);
    q[LORA_TP_ACK_STATUS_OFF] = status;
    q[LORA_TP_ACK_STATUS_OFF + 1] = 0;
    put32(q + LORA_TP_ACK_CONTIG_OFF, contiguous);
    send_frame(c, frame, sizeof(frame));
}

static void rx_reset(lora_tp_ctx_t* c) {
    rx_sess_t* r = &c->rx;
    if (r->ram && c->deps.mem_free) {
        c->deps.mem_free(r->ram);
    }
    memset(r, 0, sizeof(*r));
}

static void rx_deliver(lora_tp_ctx_t* c, bool crc_ok) {
    rx_sess_t* r = &c->rx;
    if (crc_ok) {
        c->rx_have_last = true;
        c->rx_last_src = r->src;
        c->rx_last_msg = r->msg_id;
        c->rx_last_total = r->total;
    }
    if (!crc_ok) {
        LOGW(TAG, "rx crc32 mismatch (msg_id=%u), reject", r->msg_id);
        c->stats.rx_crc_err++;
        rx_send_ack(c, r->src, r->msg_id, r->contiguous, LORA_TP_ACK_ERR);
        rx_reset(c);
        return;
    }
    LOGI(TAG, "rx complete: src=0x%04X msg_id=%u len=%u%s",
         r->src, r->msg_id, (unsigned)r->total, r->ram ? "" : " (sink)");
    rx_send_ack(c, r->src, r->msg_id, r->total, LORA_TP_ACK_OK);   /* 最终累积 ACK：让发端干净收尾 */

    lora_tp_rx_info_t info = {
        .src = r->src,
        .dst = c->cfg.local_addr,
        .msg_id = r->msg_id,
        .len = r->total,
        .qos = r->qos,
        .data = r->ram,   /* sink 路径为 NULL，应用自取 */
    };
    if (c->rx_cb) {
        c->rx_cb(&info, c->rx_user);
    }
    emit_event(EVENT_LORA_TP_RX_COMPLETE, 0, r->src, r->total, LORA_TP_OK, 1000);
    c->stats.rx_ok++;
    rx_reset(c);
}

/** DATA 帧处理 */
static void handle_data(lora_tp_ctx_t* c, const uint8_t* hdr, const uint8_t* payload,
                        uint8_t payload_len, uint16_t src, uint16_t dst) {
    if (payload_len < LORA_TP_SUBHDR) {
        LOGW(TAG, "DATA frame too short: %u", payload_len);
        return;
    }
    uint16_t msg_id = get16(payload);
    uint16_t offset = get16(payload + 2);
    uint32_t total = get32(payload + 4);
    uint32_t crc = get32(payload + 8);
    const uint8_t* chunk = payload + LORA_TP_SUBHDR;
    size_t chunk_len = payload_len - LORA_TP_SUBHDR;
    uint8_t flags = hdr[3];
    rx_sess_t* r = &c->rx;

    /* UNRELIABLE：单帧整消息，直通交付 */
    if (flags & LORA_TP_FLAG_NOACK) {
        lora_tp_rx_info_t info = {
            .src = src, .dst = dst, .msg_id = msg_id,
            .len = (uint32_t)chunk_len, .qos = LORA_TP_QOS_UNRELIABLE,
            .data = chunk,
        };
        if (c->rx_cb) {
            c->rx_cb(&info, c->rx_user);
        }
        emit_event(EVENT_LORA_TP_RX_COMPLETE, 0, src, chunk_len, LORA_TP_OK, 1000);
        c->stats.rx_ok++;
        return;
    }

    /* 幂等：已交付过的消息再次到达（对端丢了最终 ACK）→ 只补 ACK */
    if (c->rx_have_last && c->rx_last_src == src && c->rx_last_msg == msg_id) {
        c->stats.rx_dup++;
        rx_send_ack(c, src, msg_id, c->rx_last_total, LORA_TP_ACK_OK);
        return;
    }

    /* RELIABLE：新会话（src,msg_id 不匹配即新消息，旧残留丢弃） */
    if (!r->active || r->src != src || r->msg_id != msg_id) {
        if (r->active) {
            LOGW(TAG, "rx replaced: old msg_id=%u (%u/%u) dropped",
                 r->msg_id, (unsigned)r->contiguous, (unsigned)r->total);
        }
        rx_reset(c);
        r->active = true;
        r->src = src;
        r->msg_id = msg_id;
        r->total = total;
        r->crc = crc;
        r->qos = LORA_TP_QOS_RELIABLE;

        if (total == 0 || total > c->cfg.msg_max_bytes) {
            LOGW(TAG, "reject: bad total %u", (unsigned)total);
            rx_send_ack(c, src, msg_id, 0, LORA_TP_ACK_ERR);
            rx_reset(c);
            return;
        }
        if (total > c->cfg.rx_inline_max_bytes) {
            if (!c->rx_sink_valid) {
                LOGW(TAG, "reject: large msg (total=%u) without rx sink", (unsigned)total);
                rx_send_ack(c, src, msg_id, 0, LORA_TP_ACK_ERR);
                rx_reset(c);
                return;
            }
            r->ram = NULL;   /* sink 路径 */
        } else {
            r->ram = (uint8_t*)c->deps.mem_alloc(total);
            if (!r->ram) {
                LOGE(TAG, "oom for rx buffer (%u)", (unsigned)total);
                rx_send_ack(c, src, msg_id, 0, LORA_TP_ACK_ERR);
                rx_reset(c);
                return;
            }
        }
    }

    if (r->total != total || r->crc != crc) {
        LOGW(TAG, "rx meta mismatch (total/crc), ignore");
        return;
    }

    if (offset >= r->total || chunk_len == 0 ||
        (uint32_t)offset + chunk_len > r->total) {
        LOGW(TAG, "bad chunk offset=%u len=%u total=%u", offset, (unsigned)chunk_len, (unsigned)r->total);
        rx_send_ack(c, src, msg_id, r->contiguous, LORA_TP_ACK_OK);
        return;
    }

    if ((uint32_t)offset + chunk_len <= r->contiguous) {
        c->stats.rx_dup++;   /* 重复片：幂等丢弃但仍确认 */
        rx_send_ack(c, src, msg_id, r->contiguous, LORA_TP_ACK_OK);
        return;
    }
    if (offset > r->contiguous) {
        /* 乱序（前片丢失）：不落盘，回断点让发端续传 */
        rx_send_ack(c, src, msg_id, r->contiguous, LORA_TP_ACK_OK);
        return;
    }

    /* 顺序片：落盘 + 链式 CRC（严格按序接受保证链接有效） */
    r->crc_state = crc32_calc(r->crc_state, chunk, chunk_len);
    if (r->ram) {
        memcpy(r->ram + offset, chunk, chunk_len);
    } else if (c->rx_sink_valid) {
        lora_tp_err_t werr = c->rx_sink.write(c->rx_sink.ctx, offset, chunk, chunk_len);
        if (werr != LORA_TP_OK) {
            LOGW(TAG, "sink write failed: %d, reject peer", werr);
            rx_send_ack(c, src, msg_id, r->contiguous, LORA_TP_ACK_ERR);
            rx_reset(c);
            return;
        }
    } else {
        rx_send_ack(c, src, msg_id, r->contiguous, LORA_TP_ACK_ERR);
        rx_reset(c);
        return;
    }

    r->contiguous = (uint32_t)offset + (uint32_t)chunk_len;

    if (r->contiguous >= r->total) {
        /* 按序链式 CRC 与发端整条 CRC 比对（RAM/sink 路径统一） */
        bool crc_ok = (r->crc_state == r->crc);
        rx_deliver(c, crc_ok);
        return;
    }

    /* PROGRESS：收端侧不发；此处仅 ACK */
    rx_send_ack(c, src, msg_id, r->contiguous, LORA_TP_ACK_OK);
}

/** DATA_ACK 帧处理（发端累积确认 + 断点续传 + PROGRESS 节流 5%） */
static void handle_data_ack(lora_tp_ctx_t* c, const uint8_t* payload,
                            uint8_t payload_len) {
    tx_sess_t* s = &c->tx;
    if (s->state == TXS_IDLE || payload_len < LORA_TP_ACK_PAYLOAD) {
        return;
    }
    /* 注：回环/驱动可能同步投递 ACK（窗口仍在发）——累积确认在
     * TRANSFER/WAIT_ACK 任意态都处理 */
    uint16_t msg_id = get16(payload);
    if (msg_id != s->msg_id) {
        return;   /* 迟到的旧会话 ACK */
    }
    uint8_t status = payload[LORA_TP_ACK_STATUS_OFF];
    uint32_t contiguous = get32(payload + LORA_TP_ACK_CONTIG_OFF);

    if (status == LORA_TP_ACK_ERR) {
        LOGW(TAG, "peer rejected (status=ERR)");
        tx_finish(c, s, LORA_TP_ST_FAILED, LORA_TP_ERR_PEER);
        return;
    }
    if (contiguous > s->cursor && contiguous <= s->total) {
        s->cursor = contiguous;
        uint32_t pm = (uint32_t)((uint64_t)s->cursor * 1000u / s->total);
        if (pm >= s->last_progress_pm + 50 || pm >= 1000) {   /* ≥5% 台阶 */
            s->last_progress_pm = pm;
            emit_event(EVENT_LORA_TP_PROGRESS, s->token, s->req.des.addr,
                       s->total, LORA_TP_OK, pm);
        }
    } else if (contiguous < s->cursor) {
        /* 对端丢失重组状态（完成后复位/重启）：回退断点整段重传，
         * 收端按 (src,msg_id) 重建会话，旧片在乱序保护下不会误收 */
        LOGW(TAG, "peer lost state (ack=%u < cursor=%u), rewinding",
             (unsigned)contiguous, (unsigned)s->cursor);
        s->cursor = contiguous;
        s->last_progress_pm = 0;
    }
    s->retries = 0;
    if (s->cursor >= s->total) {
        tx_finish(c, s, LORA_TP_ST_DONE, LORA_TP_OK);
    } else if (s->state == TXS_WAIT_ACK) {
        s->state = TXS_TRANSFER;
    }
}

/** 帧入口：解析 §2.1 帧头并分发（drv rx 回调 / mock 注入共用） */
void lora_tp_inst_on_frame(lora_tp_ctx_t* c, const uint8_t* data, size_t len,
                           int8_t rssi) {
    if (!c || !c->inited || !data || len < LORA_TP_FRAME_HDR) {
        return;
    }
    if (data[0] != LORA_TP_FRAME_MAGIC || data[1] != LORA_TP_PROTO_VER) {
        return;
    }
    uint16_t dst = get16(data + 4);
    uint16_t src = get16(data + 6);
    if (dst != c->cfg.local_addr && dst != LORA_TP_ADDR_BCAST) {
        return;   /* 寻址过滤：非本机/广播静默丢弃 */
    }
    uint8_t payload_len = data[10];
    if (LORA_TP_FRAME_HDR + payload_len != len) {
        return;   /* 长度不符 */
    }
    (void)rssi;
    const uint8_t* payload = data + LORA_TP_FRAME_HDR;

    switch (data[2]) {
    case LORA_TP_FRM_DATA:
        handle_data(c, data, payload, payload_len, src, dst);
        break;
    case LORA_TP_FRM_DATA_ACK:
        handle_data_ack(c, payload, payload_len);
        break;
    default:
        break;   /* VOICE/CMD/BEACON 等属应用层/其他批次 */
    }
}

/** drv rx 回调蹦床（user=实例指针） */
static void drv_rx_tramp(const uint8_t* data, size_t len, int8_t rssi, void* user) {
    lora_tp_inst_on_frame((lora_tp_ctx_t*)user, data, len, rssi);
}

/* ========================================================================== */
/*                          实例 API（公共单例委托）                            */
/* ========================================================================== */

size_t lora_tp_inst_ctx_size(void) {
    return sizeof(struct lora_tp_ctx);
}

lora_tp_ctx_t* lora_tp_inst_ctx(void* mem, size_t mem_size) {
    if (!mem || mem_size < sizeof(struct lora_tp_ctx)) {
        return NULL;
    }
    return (lora_tp_ctx_t*)mem;
}

lora_tp_err_t lora_tp_inst_init(lora_tp_ctx_t* c, const lora_tp_deps_t* deps,
                                const lora_tp_cfg_t* cfg) {
    if (!c || !deps || !deps->drv || !deps->drv->send || !deps->drv->register_rx ||
        !deps->tick_ms || !deps->mem_alloc || !deps->mem_free) {
        return LORA_TP_ERR_PARAM;
    }
    memset(c, 0, sizeof(*c));
    c->deps = *deps;
    lora_tp_cfg_default(&c->cfg);
    if (cfg) {
        c->cfg = *cfg;   /* 显式配置优先（内部实例/测试） */
        if (c->cfg.tx_queue_depth > LORA_TP_QUEUE_MAX) {
            c->cfg.tx_queue_depth = LORA_TP_QUEUE_MAX;
        }
        if (c->cfg.window > 8) {
            c->cfg.window = 8;
        }
    } else {
        load_cfg_from_dtree(&c->cfg);   /* 公共单例：设备树覆盖缺省 */
    }
    c->next_token = 1;
    c->inited = true;

    lora_tp_err_t err = deps->drv->register_rx(deps->drv->drv_ctx, drv_rx_tramp, c);
    if (err != LORA_TP_OK) {
        LOGE(TAG, "register rx failed: %d", err);
        c->inited = false;
        return err;
    }
    LOGI(TAG, "init ok (addr=0x%04X win=%u q=%u retries=%u)",
         c->cfg.local_addr, (unsigned)c->cfg.window, (unsigned)c->cfg.tx_queue_depth, (unsigned)c->cfg.max_retries);
    return LORA_TP_OK;
}

lora_tp_err_t lora_tp_inst_deinit(lora_tp_ctx_t* c) {
    if (!c || !c->inited) {
        return LORA_TP_ERR_NOT_INIT;
    }
    rx_reset(c);
    memset(c, 0, sizeof(*c));
    return LORA_TP_OK;
}

lora_tp_err_t lora_tp_inst_send(lora_tp_ctx_t* c, const lora_tp_des_t* des,
                                const lora_tp_source_t* src, uint32_t total,
                                lora_tp_token_t* token) {
    if (!c || !c->inited || !des || !src || !src->read || !token) {
        return LORA_TP_ERR_PARAM;
    }
    if (total == 0) {
        return LORA_TP_ERR_PARAM;
    }
    if (des->qos == LORA_TP_QOS_RELIABLE && des->addr == LORA_TP_ADDR_BCAST) {
        return LORA_TP_ERR_PARAM;   /* 可靠广播不支持（§3-5） */
    }
    if (des->qos == LORA_TP_QOS_UNRELIABLE && total > LORA_TP_CHUNK_MAX) {
        return LORA_TP_ERR_TOO_LARGE;   /* 单帧载荷上限 */
    }
    if (des->profile > LORA_TP_PROFILE_SLOW) {
        return LORA_TP_ERR_PARAM;
    }
    if (des->qos == LORA_TP_QOS_RELIABLE && total > c->cfg.msg_max_bytes) {
        return LORA_TP_ERR_TOO_LARGE;
    }
    if (c->queue_len >= c->cfg.tx_queue_depth) {
        LOGW(TAG, "tx queue full (%u)", c->queue_len);
        return LORA_TP_ERR_BUSY;
    }

    tx_req_t* r = &c->queue[c->queue_len++];
    r->des = *des;
    r->src = *src;
    r->total = total;
    r->token = c->next_token++;
    *token = r->token;
    LOGD(TAG, "queued: token=%u total=%u qos=%d", (unsigned)r->token, (unsigned)total, des->qos);
    return LORA_TP_OK;
}

lora_tp_err_t lora_tp_inst_cancel(lora_tp_ctx_t* c, lora_tp_token_t token) {
    if (!c || !c->inited) {
        return LORA_TP_ERR_NOT_INIT;
    }
    if (token == 0) {
        return LORA_TP_ERR_PARAM;
    }
    for (uint8_t i = 0; i < c->queue_len; i++) {
        if (c->queue[i].token == token) {
            memmove(&c->queue[i], &c->queue[i + 1],
                    sizeof(tx_req_t) * (size_t)(c->queue_len - i - 1));
            c->queue_len--;
            record_done(c, token, LORA_TP_ST_CANCELLED, 0);
            return LORA_TP_OK;
        }
    }
    if (c->tx.state != TXS_IDLE && c->tx.token == token) {
        tx_finish(c, &c->tx, LORA_TP_ST_CANCELLED, LORA_TP_ERR_CANCELLED);
        return LORA_TP_OK;
    }
    return LORA_TP_ERR_PARAM;
}

lora_tp_err_t lora_tp_inst_query(lora_tp_ctx_t* c, lora_tp_token_t token,
                                 lora_tp_state_t* st, uint32_t* permille) {
    if (!c || !c->inited || !st || token == 0) {
        return LORA_TP_ERR_PARAM;
    }
    uint32_t pm = 0;
    if (c->tx.state != TXS_IDLE && c->tx.token == token) {
        *st = LORA_TP_ST_RUNNING;
        pm = (c->tx.total)
                 ? (uint32_t)((uint64_t)c->tx.cursor * 1000u / c->tx.total)
                 : 0;
    } else if (c->last_done.token == token) {
        *st = c->last_done.st;
        pm = (*st == LORA_TP_ST_DONE) ? 1000 : 0;
    } else {
        return LORA_TP_ERR_PARAM;   /* 未知 token */
    }
    if (permille) {
        *permille = pm;
    }
    return LORA_TP_OK;
}

lora_tp_err_t lora_tp_inst_register_rx(lora_tp_ctx_t* c, lora_tp_rx_cb_t cb, void* user) {
    if (!c || !c->inited) {
        return LORA_TP_ERR_NOT_INIT;
    }
    c->rx_cb = cb;
    c->rx_user = user;
    return LORA_TP_OK;
}

lora_tp_err_t lora_tp_inst_set_rx_sink(lora_tp_ctx_t* c, const lora_tp_sink_t* sink) {
    if (!c || !c->inited) {
        return LORA_TP_ERR_NOT_INIT;
    }
    if (sink && sink->write) {
        c->rx_sink = *sink;
        c->rx_sink_valid = true;
    } else {
        c->rx_sink_valid = false;
    }
    return LORA_TP_OK;
}

lora_tp_err_t lora_tp_inst_get_stats(lora_tp_ctx_t* c, lora_tp_stats_t* s) {
    if (!c || !c->inited || !s) {
        return LORA_TP_ERR_PARAM;
    }
    *s = c->stats;
    return LORA_TP_OK;
}

/* ========================================================================== */
/*                          公共单例 API（design §2 冻结集）                    */
/* ========================================================================== */

lora_tp_err_t lora_tp_init(const lora_tp_deps_t* deps) {
    if (s_inst && s_inst->inited) {
        return LORA_TP_OK;   /* 幂等 */
    }
    s_inst = lora_tp_inst_ctx(s_inst_mem, sizeof(s_inst_mem));
    return lora_tp_inst_init(s_inst, deps, NULL);   /* NULL → 设备树/缺省 */
}

lora_tp_err_t lora_tp_deinit(void) {
    if (!s_inst || !s_inst->inited) {
        return LORA_TP_ERR_NOT_INIT;
    }
    return lora_tp_inst_deinit(s_inst);
}

void lora_tp_tick(void) {
    lora_tp_inst_tick(s_inst);
}

lora_tp_err_t lora_tp_send(const lora_tp_des_t* des, const lora_tp_source_t* src,
                           uint32_t total, lora_tp_token_t* token) {
    return lora_tp_inst_send(s_inst, des, src, total, token);
}

lora_tp_err_t lora_tp_cancel(lora_tp_token_t token) {
    return lora_tp_inst_cancel(s_inst, token);
}

lora_tp_err_t lora_tp_query(lora_tp_token_t token, lora_tp_state_t* st, uint32_t* permille) {
    return lora_tp_inst_query(s_inst, token, st, permille);
}

lora_tp_err_t lora_tp_register_rx(lora_tp_rx_cb_t cb, void* user) {
    return lora_tp_inst_register_rx(s_inst, cb, user);
}

lora_tp_err_t lora_tp_set_rx_sink(const lora_tp_sink_t* sink) {
    return lora_tp_inst_set_rx_sink(s_inst, sink);
}

lora_tp_err_t lora_tp_get_stats(lora_tp_stats_t* s) {
    return lora_tp_inst_get_stats(s_inst, s);
}
