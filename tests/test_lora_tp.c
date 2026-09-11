/**
 * @file test_lora_tp.c
 * @brief [HUB] 批次③ lora_tp 回环门禁（mock 驱动双实例 A/B 互发，
 *        design §7 验收六场景）
 *
 *   L1 成功路径：8KB 消息传输成功且逐字节一致；
 *   L2 丢帧 30%：重传后成功，rx_dup 计数正确；
 *   L3 断链续传：mock 停止转发一段时间→恢复后从断点完成；
 *   L4 CRC 注入：收端拒收（ACK_ERR）→对端 FAILED(ERR_PEER)；
 *   L5 广播：UNRELIABLE 广播直达；RELIABLE 广播被拒（ERR_PARAM）；
 *   L6 大消息 sink 路径：>rx_inline_max 走 sink 落盘；RAM 路径超限
 *      被拒（ERR_TOO_LARGE / 对端 FAILED）。
 *
 * 双实例经 loop mock 交叉路由（A.send→B.on_frame，B.send→A.on_frame），
 * 每轮测试后 tick 泵推进状态机（零真时钟依赖，确定性）。
 */

#include "lora_tp_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_pass = 0, s_fail = 0;
#define CHECK(cond) do { \
    if (cond) { s_pass++; } \
    else { s_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ------------------------------------------------------------------ */
/* mock 内存池 / 时钟                                                   */
/* ------------------------------------------------------------------ */

static uint8_t s_pool[128 * 1024];
static size_t s_pool_used = 0;
static uint64_t s_fake_ms = 1000;

static void* mock_alloc(size_t n) {
    if (s_pool_used + n > sizeof(s_pool)) {
        return NULL;
    }
    void* p = &s_pool[s_pool_used];
    s_pool_used += (n + 7) & ~(size_t)7;
    return p;
}
static void mock_free(void* p) {
    (void)p;   /* 池式分配，测试期不回收 */
}
static uint32_t mock_tick_ms(void) {
    return (uint32_t)s_fake_ms;
}
static void mock_delay(uint32_t ms) {
    s_fake_ms += ms;
}

/* 推进虚拟时间并把两个实例的 tick 泵到稳态 */
static void pump(lora_tp_ctx_t* a, lora_tp_ctx_t* b, int rounds) {
    for (int i = 0; i < rounds; i++) {
        s_fake_ms += 10;
        lora_tp_inst_tick(a);
        lora_tp_inst_tick(b);
    }
}

/* ------------------------------------------------------------------ */
/* loop mock 驱动：交叉路由 + 故障注入                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    lora_tp_ctx_t* peer;
    int drop_pct;          /* 0~99：按伪随机丢帧 */
    uint32_t blackout_ms;  /* 该时刻之后停止转发（0=不限） */
    uint32_t resume_ms;    /* 转发恢复时刻 */
    int corrupt_crc;       /* 非0：对 DATA 帧载荷翻转一次 */
    uint32_t sent;
    uint32_t dropped;
} loop_drv_t;

static uint32_t s_prng = 0x12345678;
static uint32_t prng(void) {
    s_prng = s_prng * 1103515245u + 12345u;
    return (s_prng >> 16);
}

static lora_tp_err_t loop_send(void* ctx, const void* data, size_t len) {
    loop_drv_t* d = (loop_drv_t*)ctx;
    d->sent++;
    if (d->blackout_ms != 0 && s_fake_ms >= d->blackout_ms && s_fake_ms < d->resume_ms) {
        d->dropped++;
        return LORA_TP_OK;   /* 静默吞掉（模拟链路中断） */
    }
    if (d->drop_pct > 0 && (int)(prng() % 100u) < d->drop_pct) {
        d->dropped++;
        return LORA_TP_OK;
    }
    uint8_t buf[512];
    if (len > sizeof(buf)) {
        return LORA_TP_ERR_PARAM;
    }
    memcpy(buf, data, len);
    if (d->corrupt_crc && len > 20) {
        buf[len - 1] ^= 0xFF;   /* 破坏载荷尾部（消息数据/CRC 域） */
        d->corrupt_crc--;
    }
    lora_tp_inst_on_frame(d->peer, buf, len, -80);
    return LORA_TP_OK;
}

static lora_tp_err_t loop_reg_rx(void* ctx, void (*cb)(const uint8_t*, size_t, int8_t, void*),
                                 void* user) {
    (void)ctx; (void)cb; (void)user;
    return LORA_TP_OK;   /* 回环不走回调注册路径 */
}

/* ------------------------------------------------------------------ */
/* source/sink 助手                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t* buf;
    uint32_t size;
    uint32_t fail_at;   /* >=0：从该 offset 起读失败（IO 注入，未用） */
} mem_src_t;

static lora_tp_err_t src_read(void* ctx, uint32_t offset, void* buf,
                              size_t len, size_t* out) {
    mem_src_t* m = (mem_src_t*)ctx;
    if (offset >= m->size) {
        *out = 0;
        return LORA_TP_OK;
    }
    size_t n = m->size - offset;
    if (n > len) {
        n = len;
    }
    memcpy(buf, m->buf + offset, n);
    *out = n;
    return LORA_TP_OK;
}

typedef struct {
    uint8_t* buf;
    uint32_t size;
    int fail_next;   /* 非0：接下来 N 次写失败 */
} mem_sink_t;

static lora_tp_err_t sink_write(void* ctx, uint32_t offset, const void* buf, size_t len) {
    mem_sink_t* m = (mem_sink_t*)ctx;
    if (m->fail_next > 0) {
        m->fail_next--;
        return LORA_TP_ERR_PEER;
    }
    if (offset + len > m->size) {
        return LORA_TP_ERR_PARAM;
    }
    memcpy(m->buf + offset, buf, len);
    return LORA_TP_OK;
}

/* ------------------------------------------------------------------ */
/* 实例搭建                                                             */
/* ------------------------------------------------------------------ */

static loop_drv_t s_drv_a, s_drv_b;
static lora_drv_api_t s_api_a, s_api_b;
static uint8_t s_ctx_mem_a[4096], s_ctx_mem_b[4096];

/* RX 观察 */
static uint8_t s_rx_buf[96 * 1024];
static volatile int s_rx_count;
static uint16_t s_rx_last_src;
static uint32_t s_rx_last_len;
static const void* s_rx_last_data;

static void rx_capture(const lora_tp_rx_info_t* info, void* user) {
    (void)user;
    if (info->data && info->len <= sizeof(s_rx_buf)) {
        memcpy(s_rx_buf, info->data, info->len);
    }
    s_rx_last_src = info->src;
    s_rx_last_len = info->len;
    s_rx_last_data = info->data;
    s_rx_count++;
}

static lora_tp_err_t setup_pair(uint16_t addr_a, uint16_t addr_b, uint32_t inline_max) {
    memset(s_ctx_mem_a, 0, sizeof(s_ctx_mem_a));
    memset(s_ctx_mem_b, 0, sizeof(s_ctx_mem_b));
    memset(&s_drv_a, 0, sizeof(s_drv_a));
    memset(&s_drv_b, 0, sizeof(s_drv_b));
    s_pool_used = 0;
    s_rx_count = 0;
    s_prng = 0x12345678;
    s_fake_ms = 1000;

    if (lora_tp_inst_ctx_size() > sizeof(s_ctx_mem_a)) {
        return LORA_TP_ERR_NO_MEM;
    }
    lora_tp_cfg_t cfg;
    lora_tp_cfg_default(&cfg);
    cfg.local_addr = addr_a;
    cfg.rx_inline_max_bytes = inline_max;
    cfg.window = 4;
    cfg.max_retries = 3;
    cfg.ack_timeout_ms[0] = 200;   /* 测试用短超时（配合 pump 虚拟时钟） */
    cfg.tx_gap_ms[0] = 0;
    s_api_a.drv_ctx = &s_drv_a;
    s_api_a.send = loop_send;
    s_api_a.register_rx = loop_reg_rx;
    lora_tp_deps_t deps_a = {
        .drv = &s_api_a,
        .tick_ms = mock_tick_ms, .delay_ms = mock_delay,
        .mem_alloc = mock_alloc, .mem_free = mock_free,
    };
    lora_tp_err_t err = lora_tp_inst_init(lora_tp_inst_ctx(s_ctx_mem_a, sizeof(s_ctx_mem_a)),
                                          &deps_a, &cfg);
    if (err != LORA_TP_OK) {
        return err;
    }

    lora_tp_cfg_t cfg_b = cfg;
    cfg_b.local_addr = addr_b;
    s_api_b.drv_ctx = &s_drv_b;
    s_api_b.send = loop_send;
    s_api_b.register_rx = loop_reg_rx;
    lora_tp_deps_t deps_b = {
        .drv = &s_api_b,
        .tick_ms = mock_tick_ms, .delay_ms = mock_delay,
        .mem_alloc = mock_alloc, .mem_free = mock_free,
    };
    err = lora_tp_inst_init(lora_tp_inst_ctx(s_ctx_mem_b, sizeof(s_ctx_mem_b)),
                            &deps_b, &cfg_b);
    if (err != LORA_TP_OK) {
        return err;
    }

    /* 循环依赖：实例初始化后互相指向对方 */
    s_drv_a.peer = lora_tp_inst_ctx(s_ctx_mem_b, sizeof(s_ctx_mem_b));
    s_drv_b.peer = lora_tp_inst_ctx(s_ctx_mem_a, sizeof(s_ctx_mem_a));

    lora_tp_inst_register_rx(lora_tp_inst_ctx(s_ctx_mem_b, sizeof(s_ctx_mem_b)),
                             rx_capture, NULL);
    return LORA_TP_OK;
}

/* 注意：deps 内嵌临时 lora_drv_api_t 的生存期——init 时已拷贝整个结构，OK */

static lora_tp_ctx_t* A(void) {
    return lora_tp_inst_ctx(s_ctx_mem_a, sizeof(s_ctx_mem_a));
}
static lora_tp_ctx_t* B(void) {
    return lora_tp_inst_ctx(s_ctx_mem_b, sizeof(s_ctx_mem_b));
}

/* ------------------------------------------------------------------ */

static void fill_pattern(uint8_t* buf, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        buf[i] = (uint8_t)(i * 31 + (i >> 8));
    }
}

/* L1：8KB 成功路径 */
static void test_l1_success_8k(void) {
    printf("[L1] reliable 8KB transfer success\n");
    CHECK(setup_pair(0x0001, 0x0002, 16384) == LORA_TP_OK);   /* RAM 重组路径 */

    static uint8_t msg[8192];
    fill_pattern(msg, sizeof(msg));
    mem_src_t src = { .buf = msg, .size = sizeof(msg) };

    lora_tp_des_t des = { .addr = 0x0002, .qos = LORA_TP_QOS_RELIABLE,
                          .profile = LORA_TP_PROFILE_NORMAL };
    lora_tp_token_t token = 0;
    CHECK(lora_tp_inst_send(A(), &des, &(lora_tp_source_t){ .ctx = &src, .read = src_read },
                            sizeof(msg), &token) == LORA_TP_OK);
    CHECK(token != 0);

    pump(A(), B(), 20000);
    lora_tp_state_t st = LORA_TP_ST_RUNNING;
    uint32_t pm = 0;
    CHECK(lora_tp_inst_query(A(), token, &st, &pm) == LORA_TP_OK);
    CHECK(st == LORA_TP_ST_DONE);
    CHECK(pm == 1000);

    CHECK(s_rx_count == 1);
    CHECK(s_rx_last_src == 0x0001);
    CHECK(s_rx_last_len == sizeof(msg));
    CHECK(memcmp(s_rx_buf, msg, sizeof(msg)) == 0);   /* 逐字节一致 */

    lora_tp_stats_t stats;
    CHECK(lora_tp_inst_get_stats(B(), &stats) == LORA_TP_OK);
    CHECK(stats.rx_ok == 1);
    CHECK(stats.rx_crc_err == 0);
}

/* L2：丢帧 30% 仍成功 */
static void test_l2_loss_30pct(void) {
    printf("[L2] 30%% frame loss -> still succeeds via ARQ\n");
    CHECK(setup_pair(0x0001, 0x0002, 16384) == LORA_TP_OK);
    s_drv_a.drop_pct = 30;
    s_drv_b.drop_pct = 30;

    static uint8_t msg[4096];
    fill_pattern(msg, sizeof(msg));
    mem_src_t src = { .buf = msg, .size = sizeof(msg) };

    lora_tp_des_t des = { .addr = 0x0002, .qos = LORA_TP_QOS_RELIABLE,
                          .profile = LORA_TP_PROFILE_NORMAL };
    lora_tp_token_t token = 0;
    CHECK(lora_tp_inst_send(A(), &des, &(lora_tp_source_t){ .ctx = &src, .read = src_read },
                            sizeof(msg), &token) == LORA_TP_OK);

    pump(A(), B(), 200000);
    lora_tp_state_t st = LORA_TP_ST_RUNNING;
    lora_tp_inst_query(A(), token, &st, NULL);
    CHECK(st == LORA_TP_ST_DONE);
    CHECK(s_rx_count == 1);
    CHECK(memcmp(s_rx_buf, msg, sizeof(msg)) == 0);

    lora_tp_stats_t stats, txstats;
    lora_tp_inst_get_stats(B(), &stats);
    lora_tp_inst_get_stats(A(), &txstats);
    /* 丢帧必致重传/重复片二者其一 */
    CHECK(stats.rx_dup > 0 || txstats.frames_retx > 0);
    CHECK(stats.rx_crc_err == 0);
}

/* L3：断链（blackout）后从断点续传完成 */
static void test_l3_blackout_resume(void) {
    printf("[L3] blackout -> resume from breakpoint\n");
    CHECK(setup_pair(0x0001, 0x0002, 16384) == LORA_TP_OK);
    /* 2s 后断链 300ms（重试预算内），恢复后须从断点续传完成。
     * （断链时长 > max_retries×ack_timeout 属协议内合法失败，
     *   由 FAILED(TIMEOUT) 表达，不归本场景） */
    s_drv_a.blackout_ms = 2000;
    s_drv_a.resume_ms = 2300;

    static uint8_t msg[8192];
    fill_pattern(msg, sizeof(msg));
    mem_src_t src = { .buf = msg, .size = sizeof(msg) };

    lora_tp_des_t des = { .addr = 0x0002, .qos = LORA_TP_QOS_RELIABLE,
                          .profile = LORA_TP_PROFILE_NORMAL };
    lora_tp_token_t token = 0;
    CHECK(lora_tp_inst_send(A(), &des, &(lora_tp_source_t){ .ctx = &src, .read = src_read },
                            sizeof(msg), &token) == LORA_TP_OK);

    pump(A(), B(), 300000);
    lora_tp_state_t st = LORA_TP_ST_RUNNING;
    lora_tp_inst_query(A(), token, &st, NULL);
    CHECK(st == LORA_TP_ST_DONE);
    CHECK(s_rx_count == 1);
    CHECK(memcmp(s_rx_buf, msg, sizeof(msg)) == 0);
}

/* L4：CRC 注入 → 收端拒收 → 对端 FAILED(ERR_PEER) */
static void test_l4_crc_reject(void) {
    printf("[L4] crc corruption -> peer FAILED(ERR_PEER)\n");
    CHECK(setup_pair(0x0001, 0x0002, 2048) == LORA_TP_OK);
    /* 让某一帧载荷翻转：收端算出的整条 CRC 不匹配 */
    s_drv_a.corrupt_crc = 1;

    static uint8_t msg[600];   /* 多片消息（>188B） */
    fill_pattern(msg, sizeof(msg));
    mem_src_t src = { .buf = msg, .size = sizeof(msg) };

    lora_tp_des_t des = { .addr = 0x0002, .qos = LORA_TP_QOS_RELIABLE,
                          .profile = LORA_TP_PROFILE_NORMAL };
    lora_tp_token_t token = 0;
    CHECK(lora_tp_inst_send(A(), &des, &(lora_tp_source_t){ .ctx = &src, .read = src_read },
                            sizeof(msg), &token) == LORA_TP_OK);

    pump(A(), B(), 100000);
    lora_tp_state_t st = LORA_TP_ST_RUNNING;
    lora_tp_inst_query(A(), token, &st, NULL);
    CHECK(st == LORA_TP_ST_FAILED);
    CHECK(s_rx_count == 0);   /* 收端不交付 */
    lora_tp_stats_t stats;
    lora_tp_inst_get_stats(B(), &stats);
    CHECK(stats.rx_crc_err >= 1);
    CHECK(stats.rx_ok == 0);
}

/* L5：广播规则 */
static void test_l5_broadcast_rules(void) {
    printf("[L5] unreliable bcast ok / reliable bcast rejected\n");
    CHECK(setup_pair(0x0001, 0x0002, 2048) == LORA_TP_OK);
    lora_tp_inst_register_rx(A(), rx_capture, NULL);   /* A 也能收广播 */

    uint8_t msg[100];
    fill_pattern(msg, sizeof(msg));
    mem_src_t src = { .buf = msg, .size = sizeof(msg) };

    /* RELIABLE 广播：API 拒绝 */
    lora_tp_des_t bad = { .addr = LORA_TP_ADDR_BCAST, .qos = LORA_TP_QOS_RELIABLE,
                          .profile = LORA_TP_PROFILE_NORMAL };
    lora_tp_token_t token = 0;
    CHECK(lora_tp_inst_send(A(), &bad, &(lora_tp_source_t){ .ctx = &src, .read = src_read },
                            sizeof(msg), &token) == LORA_TP_ERR_PARAM);

    /* UNRELIABLE 广播：B 直达 */
    lora_tp_des_t bcast = { .addr = LORA_TP_ADDR_BCAST, .qos = LORA_TP_QOS_UNRELIABLE,
                            .profile = LORA_TP_PROFILE_FAST };
    CHECK(lora_tp_inst_send(A(), &bcast, &(lora_tp_source_t){ .ctx = &src, .read = src_read },
                            sizeof(msg), &token) == LORA_TP_OK);
    pump(A(), B(), 100);
    CHECK(s_rx_count == 1);
    CHECK(s_rx_last_src == 0x0001);
    CHECK(s_rx_last_len == sizeof(msg));
    CHECK(memcmp(s_rx_buf, msg, sizeof(msg)) == 0);

    /* UNRELIABLE 超单帧上限：TOO_LARGE */
    static uint8_t big[500];
    mem_src_t bsrc = { .buf = big, .size = sizeof(big) };
    CHECK(lora_tp_inst_send(A(), &bcast, &(lora_tp_source_t){ .ctx = &bsrc, .read = src_read },
                            sizeof(big), &token) == LORA_TP_ERR_TOO_LARGE);
}

/* L6：大消息 sink 路径 + RAM 超限拒收 */
static void test_l6_sink_path(void) {
    printf("[L6] large message via sink / inline limit reject\n");
    CHECK(setup_pair(0x0001, 0x0002, 2048) == LORA_TP_OK);

    /* 6KB > inline_max(2048)：B 未设 sink → 首帧即被拒 → A FAILED(PEER) */
    static uint8_t msg[6144];
    fill_pattern(msg, sizeof(msg));
    mem_src_t src = { .buf = msg, .size = sizeof(msg) };
    lora_tp_des_t des = { .addr = 0x0002, .qos = LORA_TP_QOS_RELIABLE,
                          .profile = LORA_TP_PROFILE_NORMAL };
    lora_tp_token_t token = 0;
    CHECK(lora_tp_inst_send(A(), &des, &(lora_tp_source_t){ .ctx = &src, .read = src_read },
                            sizeof(msg), &token) == LORA_TP_OK);
    pump(A(), B(), 20000);
    lora_tp_state_t st = LORA_TP_ST_RUNNING;
    lora_tp_inst_query(A(), token, &st, NULL);
    CHECK(st == LORA_TP_ST_FAILED);
    CHECK(s_rx_count == 0);

    /* B 设置 sink → 走落盘路径完成 */
    static uint8_t sink_buf[96 * 1024];
    mem_sink_t sink = { .buf = sink_buf, .size = sizeof(sink_buf) };
    CHECK(lora_tp_inst_set_rx_sink(B(), &(lora_tp_sink_t){ .ctx = &sink, .write = sink_write })
              == LORA_TP_OK);

    CHECK(lora_tp_inst_send(A(), &des, &(lora_tp_source_t){ .ctx = &src, .read = src_read },
                            sizeof(msg), &token) == LORA_TP_OK);
    pump(A(), B(), 60000);
    st = LORA_TP_ST_RUNNING;
    lora_tp_inst_query(A(), token, &st, NULL);
    CHECK(st == LORA_TP_ST_DONE);
    CHECK(s_rx_count == 1);
    CHECK(s_rx_last_data == NULL);   /* sink 路径 data=NULL */
    CHECK(memcmp(sink_buf, msg, sizeof(msg)) == 0);   /* 落盘内容一致 */

    /* RAM 路径超 msg_max：发送端本地 TOO_LARGE */
    mem_src_t huge = { .buf = msg, .size = 70000 };
    CHECK(lora_tp_inst_send(A(), &des, &(lora_tp_source_t){ .ctx = &huge, .read = src_read },
                            70000, &token) == LORA_TP_ERR_TOO_LARGE);
}

/* 附加：取消/查询/队列满 */
static void test_l7_misc(void) {
    printf("[L7] cancel / query unknown / queue full\n");
    CHECK(setup_pair(0x0001, 0x0002, 2048) == LORA_TP_OK);

    static uint8_t msg[4000];
    fill_pattern(msg, sizeof(msg));

    /* 队列深度 4：塞满后 BUSY */
    lora_tp_des_t des = { .addr = 0x0002, .qos = LORA_TP_QOS_RELIABLE,
                          .profile = LORA_TP_PROFILE_NORMAL };
    lora_tp_token_t tokens[5];
    for (int i = 0; i < 4; i++) {
        CHECK(lora_tp_inst_send(A(), &des, &(lora_tp_source_t){ .ctx = &msg, .read = src_read },
                                sizeof(msg), &tokens[i]) == LORA_TP_OK);
    }
    CHECK(lora_tp_inst_send(A(), &des, &(lora_tp_source_t){ .ctx = &msg, .read = src_read },
                            sizeof(msg), &tokens[4]) == LORA_TP_ERR_BUSY);

    /* 队列中取消 */
    CHECK(lora_tp_inst_cancel(A(), tokens[3]) == LORA_TP_OK);
    lora_tp_state_t st = LORA_TP_ST_RUNNING;
    CHECK(lora_tp_inst_query(A(), tokens[3], &st, NULL) == LORA_TP_OK);
    CHECK(st == LORA_TP_ST_CANCELLED);

    /* 未知 token */
    CHECK(lora_tp_inst_query(A(), 0x9999, &st, NULL) == LORA_TP_ERR_PARAM);
    CHECK(lora_tp_inst_cancel(A(), 0) == LORA_TP_ERR_PARAM);

    /* 参数防御 */
    CHECK(lora_tp_inst_send(A(), NULL, NULL, 100, &tokens[0]) == LORA_TP_ERR_PARAM);
    CHECK(lora_tp_inst_send(A(), &des, &(lora_tp_source_t){ .ctx = &msg, .read = src_read },
                            0, &tokens[0]) == LORA_TP_ERR_PARAM);
}

void test_lora_tp_run(int* pass, int* fail) {
    test_l1_success_8k();
    test_l2_loss_30pct();
    test_l3_blackout_resume();
    test_l4_crc_reject();
    test_l5_broadcast_rules();
    test_l6_sink_path();
    test_l7_misc();
    *pass += s_pass;
    *fail += s_fail;
}
