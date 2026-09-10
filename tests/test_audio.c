/**
 * @file test_audio.c
 * @brief 音频 T2 升级 PC 门禁测试（mock i2s + mock dtree）
 *
 * 覆盖 idle_modules_plan §2.4 第 1 条全部场景：
 *   A1 异步播放→完成回调+drain 语义；A2 音量 0/50/100 饱和缩放；
 *   A3 播放中 abort（stop）→ABORTED+丢弃；A4 队列依次播放+事件序列；
 *   A5 文件播放/缺文件 IO 错误；A6 音量 get/set + 进度事件节流存在性。
 */

#include "audio_module.h"
#include "audio_player.h"
#include "event_bus.h"
#include "tasker.h"
#include "mem.h"

#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>

static int s_pass = 0, s_fail = 0;
#define CHECK(cond) do { \
    if (cond) { s_pass++; } \
    else { s_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void wait_ms(int ms) {
    for (int i = 0; i < ms; i += 2) {
        usleep(2000);
    }
}

/* i2s_mock.c 导出 */
void i2s_mock_reset(void);
size_t i2s_mock_capture_len(void);
size_t i2s_mock_capture_copy(uint8_t* out, size_t cap);

/* dtree_mock.c 导出 */
void dtree_mock_reset(void);
void dtree_mock_set_int(const char* compat, const char* prop, int32_t value);

/* ==================== A1: 模块级异步播放 + 完成回调 + drain ==================== */

static atomic_int s_cb_calls = 0;
static audio_module_stream_status_t s_cb_status = AUDIO_MODULE_STREAM_ERROR;
static uint32_t s_cb_total = 0;

static void on_stream_done(audio_module_stream_status_t status, uint32_t total, void* ud) {
    (void)ud;
    s_cb_status = status;
    s_cb_total = total;
    atomic_fetch_add(&s_cb_calls, 1);
}

static void test_a1_module_async(void) {
    printf("[A1] audio_module async write + done callback + drain\n");
    i2s_mock_reset();
    dtree_mock_reset();   /* 全部走缺省：ring=2048帧(4KB), vol=80, sd=-1 */

    audio_module_handle_t amp = NULL;
    CHECK(audio_module_init(&amp) == AUDIO_MODULE_OK);
    CHECK(audio_module_set_play_done_cb(amp, on_stream_done, NULL) == AUDIO_MODULE_OK);

    /* 10KB 数据分块异步写入（环形缓冲 4KB，预期部分 NO_SPACE 重试） */
    static uint8_t pcm[10240];
    for (size_t i = 0; i < sizeof(pcm); i++) {
        pcm[i] = (uint8_t)(i & 0xFF);
    }

    size_t sent = 0;
    int wait = 0;
    while (sent < sizeof(pcm) && wait < 10000) {
        audio_module_err_t err = audio_module_write_async(amp, pcm + sent, 1024);
        if (err == AUDIO_MODULE_OK) {
            sent += 1024;
        } else if (err == AUDIO_MODULE_ERR_NO_SPACE) {
            usleep(2000);
            wait += 2;
        } else {
            break;
        }
    }
    CHECK(sent == sizeof(pcm));

    /* drain：标记结束并阻塞等待播完 */
    CHECK(audio_module_drain(amp, 8000) == AUDIO_MODULE_OK);

    atomic_int deadline = 0;
    while (atomic_load(&s_cb_calls) == 0 && deadline < 8000) {
        usleep(2000);
        deadline += 2;
    }
    CHECK(atomic_load(&s_cb_calls) == 1);
    CHECK(s_cb_status == AUDIO_MODULE_STREAM_DONE);
    CHECK(s_cb_total == sizeof(pcm));
    CHECK(i2s_mock_capture_len() == sizeof(pcm));

    /* 幂等收尾：再次 drain 应立即返回 */
    CHECK(audio_module_drain(amp, 1000) == AUDIO_MODULE_OK);
}

/* ==================== A2: 音量 0/50/100 int16 饱和缩放 ==================== */

static void test_a2_volume_scaling(void) {
    printf("[A2] volume scaling (0/50/100, saturating)\n");
    audio_module_handle_t amp = NULL;
    CHECK(audio_module_init(&amp) == AUDIO_MODULE_OK);

    /* 样本模式：正/负/上下饱和边界 */
    static const int16_t pattern[8] = { 1000, -2000, 32767, -32768, 100, -100, 12345, -12345 };
    uint8_t vol = 200;

    CHECK(audio_module_get_volume(amp, &vol) == AUDIO_MODULE_OK);
    CHECK(vol == 80);   /* dtree mock 无键 → 缺省 80 */

    static const struct { uint8_t vol; int32_t scale_num; } cases[] = {
        { 0,   0 },
        { 50, 50 },
        { 100, 100 },
    };

    for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        CHECK(audio_module_set_volume(amp, cases[c].vol) == AUDIO_MODULE_OK);
        CHECK(audio_module_get_volume(amp, &vol) == AUDIO_MODULE_OK);
        CHECK(vol == cases[c].vol);

        i2s_mock_reset();
        atomic_store(&s_cb_calls, 0);
        CHECK(audio_module_write_async(amp, pattern, sizeof(pattern)) == AUDIO_MODULE_OK);
        CHECK(audio_module_drain(amp, 4000) == AUDIO_MODULE_OK);
        while (atomic_load(&s_cb_calls) == 0) {
            usleep(1000);
        }

        /* 捕获逐样本比对 */
        uint8_t cap[64];
        size_t n = i2s_mock_capture_copy(cap, sizeof(cap));
        CHECK(n == sizeof(pattern));
        for (unsigned i = 0; i < 8; i++) {
            int16_t in = pattern[i];
            int32_t expect = ((int32_t)in * cases[c].vol) / 100;
            if (expect > 32767) expect = 32767;
            if (expect < -32768) expect = -32768;
            int16_t got = (int16_t)((uint16_t)cap[i * 2] | ((uint16_t)cap[i * 2 + 1] << 8));
            if (got != (int16_t)expect) {
                CHECK(got == (int16_t)expect);
                printf("    sample[%u] in=%d vol=%u got=%d expect=%d\n",
                       i, in, cases[c].vol, got, (int)expect);
                break;
            }
        }
    }

    /* 静音=出队清零 */
    CHECK(audio_module_mute(amp, true) == AUDIO_MODULE_OK);
    i2s_mock_reset();
    atomic_store(&s_cb_calls, 0);
    CHECK(audio_module_write_async(amp, pattern, sizeof(pattern)) == AUDIO_MODULE_OK);
    CHECK(audio_module_drain(amp, 4000) == AUDIO_MODULE_OK);
    while (atomic_load(&s_cb_calls) == 0) {
        usleep(1000);
    }
    uint8_t cap[32];
    size_t n = i2s_mock_capture_copy(cap, sizeof(cap));
    CHECK(n == sizeof(pattern));
    int all_zero = 1;
    for (size_t i = 0; i < n; i++) {
        if (cap[i] != 0) {
            all_zero = 0;
        }
    }
    CHECK(all_zero);
    bool muted = true;
    CHECK(audio_module_is_muted(amp, &muted) == AUDIO_MODULE_OK);
    CHECK(muted);
    CHECK(audio_module_mute(amp, false) == AUDIO_MODULE_OK);
}

/* ==================== A3: 播放中 abort（stop）→ ABORTED + 丢弃 ==================== */

static void test_a3_abort_semantics(void) {
    printf("[A3] abort during playback -> ABORTED + discard\n");
    i2s_mock_reset();

    audio_module_handle_t amp = NULL;
    CHECK(audio_module_init(&amp) == AUDIO_MODULE_OK);
    atomic_store(&s_cb_calls, 0);   /* 清 A2 残留的回调计数 */
    CHECK(audio_module_set_play_done_cb(amp, on_stream_done, NULL) == AUDIO_MODULE_OK);

    /* 8KB 长流：喂数 20ms/tick×1KB，写完立即 abort，必有残余被丢弃 */
    static uint8_t pcm[8192];
    memset(pcm, 0x5A, sizeof(pcm));

    size_t sent = 0;
    int wait = 0;
    while (sent < sizeof(pcm) && wait < 10000) {
        audio_module_err_t err = audio_module_write_async(amp, pcm + sent, 1024);
        if (err == AUDIO_MODULE_OK) {
            sent += 1024;
        } else if (err == AUDIO_MODULE_ERR_NO_SPACE) {
            usleep(2000);
            wait += 2;
        } else {
            break;
        }
    }
    CHECK(sent == sizeof(pcm));

    CHECK(audio_module_abort_playback(amp) == AUDIO_MODULE_OK);

    int deadline = 0;
    while (atomic_load(&s_cb_calls) == 0 && deadline < 4000) {
        usleep(2000);
        deadline += 2;
    }
    CHECK(atomic_load(&s_cb_calls) == 1);
    CHECK(s_cb_status == AUDIO_MODULE_STREAM_ABORTED);
    CHECK(i2s_mock_capture_len() < sizeof(pcm));   /* 有数据被丢弃 */

    /* abort 后系统回到空闲：再次 drain 立即成功 */
    CHECK(audio_module_drain(amp, 1000) == AUDIO_MODULE_OK);

    /* abort 后可再次正常播放（流可重入） */
    i2s_mock_reset();
    atomic_store(&s_cb_calls, 0);
    CHECK(audio_module_write_async(amp, pcm, 2048) == AUDIO_MODULE_OK);
    CHECK(audio_module_drain(amp, 4000) == AUDIO_MODULE_OK);
    while (atomic_load(&s_cb_calls) == 0) {
        usleep(1000);
    }
    CHECK(s_cb_status == AUDIO_MODULE_STREAM_DONE);
    CHECK(i2s_mock_capture_len() == 2048);
}

/* ==================== 事件计数（A4/A5 用） ==================== */

#define EV_MAX 16
static atomic_int s_started_n, s_done_n, s_failed_n, s_progress_n;
static uint32_t s_started_tokens[EV_MAX], s_done_tokens[EV_MAX];
static int32_t s_last_err;

static int on_play_event(const event_t* e, void* ud) {
    const event_audio_play_t* ev = (const event_audio_play_t*)e->data;
    (void)ud;
    switch ((int)e->header.type) {
    case (int)EVENT_AUDIO_PLAY_STARTED:
        if (atomic_load(&s_started_n) < EV_MAX) {
            s_started_tokens[atomic_load(&s_started_n)] = ev->token;
        }
        atomic_fetch_add(&s_started_n, 1);
        break;
    case (int)EVENT_AUDIO_PLAY_DONE:
        if (atomic_load(&s_done_n) < EV_MAX) {
            s_done_tokens[atomic_load(&s_done_n)] = ev->token;
        }
        atomic_fetch_add(&s_done_n, 1);
        break;
    case (int)EVENT_AUDIO_PLAY_FAILED:
        s_last_err = ev->err;
        atomic_fetch_add(&s_failed_n, 1);
        break;
    case (int)EVENT_AUDIO_PLAY_PROGRESS:
        atomic_fetch_add(&s_progress_n, 1);
        break;
    default:
        break;
    }
    return 0;
}

static void reset_event_counters(void) {
    atomic_store(&s_started_n, 0);
    atomic_store(&s_done_n, 0);
    atomic_store(&s_failed_n, 0);
    atomic_store(&s_progress_n, 0);
    s_last_err = 0;
    memset(s_started_tokens, 0, sizeof(s_started_tokens));
    memset(s_done_tokens, 0, sizeof(s_done_tokens));
}

/** 构造 mem 播放项（8kHz mono 16bit，含头） */
static void build_pcm_item(uint8_t* buf, size_t data_len, int16_t fill) {
    audio_player_pcm_hdr_t hdr = {
        .magic = { 'O', 'P', 'C', 'M' },
        .rate = 8000,
        .channels = 1,
        .bits = 16,
        .data_len = (uint32_t)data_len,
    };
    memcpy(buf, &hdr, sizeof(hdr));
    int16_t* samples = (int16_t*)(buf + sizeof(hdr));
    for (size_t i = 0; i < data_len / 2; i++) {
        samples[i] = fill;
    }
}

/* ==================== A4: 队列 3 条依次播放 + 事件序列 ==================== */

static void test_a4_queue_sequential(void) {
    printf("[A4] player queue (3 items) sequential + event order\n");
    reset_event_counters();
    i2s_mock_reset();

    static uint8_t item1[2048], item2[2048], item3[2048];
    build_pcm_item(item1, sizeof(item1) - sizeof(audio_player_pcm_hdr_t), 100);
    build_pcm_item(item2, sizeof(item2) - sizeof(audio_player_pcm_hdr_t), 200);
    build_pcm_item(item3, sizeof(item3) - sizeof(audio_player_pcm_hdr_t), 300);

    audio_player_token_t t1 = 0, t2 = 0, t3 = 0;
    CHECK(audio_player_play_mem(item1, sizeof(item1), &t1) == AUDIO_PLAYER_OK);
    CHECK(audio_player_play_mem(item2, sizeof(item2), &t2) == AUDIO_PLAYER_OK);
    CHECK(audio_player_play_mem(item3, sizeof(item3), &t3) == AUDIO_PLAYER_OK);
    CHECK(t1 != 0 && t2 != 0 && t3 != 0 && t1 != t2 && t2 != t3);

    /* token 状态查询：排队/播放中可见 */
    audio_player_status_t st;
    CHECK(audio_player_get_status(t3, &st) == AUDIO_PLAYER_OK);
    CHECK(st.state == AUDIO_PLAYER_STATE_QUEUED || st.state == AUDIO_PLAYER_STATE_PLAYING);

    int deadline = 0;
    while (atomic_load(&s_done_n) < 3 && deadline < 10000) {
        usleep(2000);
        deadline += 2;
    }
    CHECK(atomic_load(&s_done_n) == 3);
    CHECK(atomic_load(&s_failed_n) == 0);
    CHECK(atomic_load(&s_started_n) == 3);
    /* 依次播放：STARTED/DONE token 序列一致且按入队顺序 */
    CHECK(s_started_tokens[0] == t1 && s_started_tokens[1] == t2 && s_started_tokens[2] == t3);
    CHECK(s_done_tokens[0] == t1 && s_done_tokens[1] == t2 && s_done_tokens[2] == t3);
    /* 时长推算：2048-12=2036B @8k mono16 ≈ 127ms */
    CHECK(atomic_load(&s_progress_n) >= 1);   /* 进度事件存在（节流 ≥10% 生效） */
}

/* ==================== A5: 文件播放 + IO 错误 ==================== */

static void test_a5_play_file(void) {
    printf("[A5] play_file + missing file IO error\n");
    reset_event_counters();
    i2s_mock_reset();

    const char* path = "/tmp/ovs_test_audio.pcm";
    static uint8_t item[1540];   /* 头 12 + 1528B 数据 @8k mono16 ≈ 95ms */
    build_pcm_item(item, sizeof(item) - sizeof(audio_player_pcm_hdr_t), 500);
    FILE* fp = fopen(path, "wb");
    CHECK(fp != NULL);
    if (fp) {
        fwrite(item, 1, sizeof(item), fp);
        fclose(fp);
    }

    audio_player_token_t t = 0;
    CHECK(audio_player_play_file(path, &t) == AUDIO_PLAYER_OK);
    CHECK(t != 0);

    int deadline = 0;
    while (atomic_load(&s_done_n) == 0 && deadline < 5000) {
        usleep(2000);
        deadline += 2;
    }
    CHECK(atomic_load(&s_done_n) == 1);
    CHECK(s_done_tokens[0] == t);
    CHECK(atomic_load(&s_failed_n) == 0);

    /* 缺文件：入队即返回 IO 错误 */
    CHECK(audio_player_play_file("/tmp/ovs_no_such_file.pcm", &t) == AUDIO_PLAYER_ERR_IO);

    remove(path);
}

/* ==================== A6: 音量 get/set + 全停清队 ==================== */

static void test_a6_volume_and_stop_all(void) {
    printf("[A6] player volume get/set + stop all\n");
    reset_event_counters();
    i2s_mock_reset();

    uint8_t vol = 0;
    audio_player_status_t st;
    /* port stub 持久化不可用 → 初始音量=设备树缺省 80 */
    CHECK(audio_player_get_volume(&vol) == AUDIO_PLAYER_OK);
    CHECK(vol == 80);

    CHECK(audio_player_set_volume(70) == AUDIO_PLAYER_OK);
    CHECK(audio_player_get_volume(&vol) == AUDIO_PLAYER_OK);
    CHECK(vol == 70);
    CHECK(audio_player_set_volume(150) == AUDIO_PLAYER_OK);   /* 越界钳位 */
    CHECK(audio_player_get_volume(&vol) == AUDIO_PLAYER_OK);
    CHECK(vol == 100);

    /* 队列 3 项 → stop(0) 全停：全部 FAILED + 队列清空 */
    static uint8_t item[2048];
    build_pcm_item(item, sizeof(item) - sizeof(audio_player_pcm_hdr_t), 300);
    audio_player_token_t t1 = 0, t2 = 0, t3 = 0;
    CHECK(audio_player_play_mem(item, sizeof(item), &t1) == AUDIO_PLAYER_OK);
    CHECK(audio_player_play_mem(item, sizeof(item), &t2) == AUDIO_PLAYER_OK);
    CHECK(audio_player_play_mem(item, sizeof(item), &t3) == AUDIO_PLAYER_OK);
    wait_ms(5);   /* 留 pump 一拍使 t1 转为当前项（或仍在队尾，两态均可） */

    CHECK(audio_player_stop(0) == AUDIO_PLAYER_OK);

    int deadline = 0;
    while ((atomic_load(&s_failed_n) + atomic_load(&s_done_n)) < 3 && deadline < 5000) {
        usleep(2000);
        deadline += 2;
    }
    CHECK((atomic_load(&s_failed_n) + atomic_load(&s_done_n)) == 3);
    CHECK(s_last_err == AUDIO_PLAYER_ERR_STOPPED);   /* FAILED 均为停止原因 */

    /* 全停后可重新入队 */
    atomic_store(&s_failed_n, 0);
    atomic_store(&s_done_n, 0);
    audio_player_token_t t4 = 0;
    CHECK(audio_player_play_mem(item, sizeof(item), &t4) == AUDIO_PLAYER_OK);
    deadline = 0;
    while (atomic_load(&s_done_n) == 0 && deadline < 5000) {
        usleep(2000);
        deadline += 2;
    }
    CHECK(atomic_load(&s_done_n) == 1);
    CHECK(atomic_load(&s_failed_n) == 0);

    /* 停不存在的 token */
    CHECK(audio_player_stop(0x7FFFFFFF) == AUDIO_PLAYER_OK);   /* 无此 token，仅日志 */
    CHECK(audio_player_get_status(0x7FFFFFFF, &st) == AUDIO_PLAYER_OK);
    CHECK(st.state == AUDIO_PLAYER_STATE_IDLE);
}

/* ==================== 入口 ==================== */

void test_audio_run(int* pass, int* fail) {
    printf("\n==== audio tests (mock i2s) ====\n");

    CHECK(event_bus_init() == EVENT_BUS_OK || event_bus_init() == EVENT_BUS_ERR_ALREADY_INIT);
    tasker_init();   /* 喂数/pump/stop 均走 tasker（首用自初始化，这里显式起） */
    CHECK(event_bus_subscribe(EVENT_AUDIO_PLAY_STARTED, on_play_event, NULL) != NULL);
    CHECK(event_bus_subscribe(EVENT_AUDIO_PLAY_DONE, on_play_event, NULL) != NULL);
    CHECK(event_bus_subscribe(EVENT_AUDIO_PLAY_FAILED, on_play_event, NULL) != NULL);
    CHECK(event_bus_subscribe(EVENT_AUDIO_PLAY_PROGRESS, on_play_event, NULL) != NULL);

    test_a1_module_async();
    test_a2_volume_scaling();
    test_a3_abort_semantics();

    /* 播放服务（复用已初始化的 audio_module） */
    CHECK(audio_player_init() == AUDIO_PLAYER_OK);
    test_a4_queue_sequential();
    test_a5_play_file();
    test_a6_volume_and_stop_all();
    CHECK(audio_player_deinit() == AUDIO_PLAYER_OK);

    *pass += s_pass;
    *fail += s_fail;
    printf("==== audio tests done: %d passed, %d failed ====\n", s_pass, s_fail);
}
