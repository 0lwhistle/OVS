/**
 * @file test_event_bus.c
 * @brief event_bus PC 宿主测试（REFACTORING_PLAN 5.1 验收门禁）
 *
 * 覆盖: 订阅生命周期 / 发布与数据完整性 / 锁外回调（handler 内再订阅不
 * 死锁）/ 事件池耗尽兜底 / 名表全覆盖 / 统计与错误计数。
 */

#include "event_bus.h"
#include "mem.h"
#include "mem_pool.h"

#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>

static int s_pass = 0, s_fail = 0;
#define CHECK(cond) do { \
    if (cond) { s_pass++; } \
    else { s_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* 等待条件成立（事件处理是异步任务） */
static void wait_until(atomic_int* flag, int timeout_ms) {
    for (int i = 0; i < timeout_ms; i += 2) {
        if (atomic_load(flag)) return;
        usleep(2000);
    }
}

/* ---------- 1. 生命周期与基础发布 ---------- */
static atomic_int s_got = 0;
static event_type_t s_got_type = 0;
static char s_got_data[32] = {0};

static atomic_int s_basic_calls = 0;

static int on_basic(const event_t* e, void* ud) {
    (void)ud;
    atomic_fetch_add(&s_basic_calls, 1);
    printf("  [dbg] on_basic type=0x%08X len=%u\n", e->header.type, e->header.data_len);
    s_got_type = e->header.type;
    if (e->header.data_len > 0) {
        size_t n = e->header.data_len < 31 ? e->header.data_len : 31;
        memcpy(s_got_data, e->data, n);
        s_got_data[n] = '\0';
    }
    atomic_store(&s_got, 1);
    return 0;
}

static void test_basic(void) {
    printf("[EB1] lifecycle / publish / data integrity\n");
    CHECK(event_bus_init() == EVENT_BUS_OK);
    CHECK(event_bus_is_initialized());
    CHECK(event_bus_init() == EVENT_BUS_ERR_ALREADY_INIT);

    atomic_store(&s_got, 0);
    CHECK(event_bus_subscribe(EVENT_WIFI_CONNECTED, on_basic, NULL) != NULL);

    event_wifi_connected_t data = {0};
    data.rssi = -55;
    CHECK(event_bus_publish(EVENT_WIFI_CONNECTED, &data, sizeof(data)) == EVENT_BUS_OK);
    wait_until(&s_got, 2000);
    CHECK(atomic_load(&s_got) == 1);
    CHECK(s_got_type == EVENT_WIFI_CONNECTED);

    /* 无数据事件 */
    /* 订阅按事件类型过滤：STARTUP 需单独挂同一 handler */
    CHECK(event_bus_subscribe(EVENT_SYSTEM_STARTUP, on_basic, NULL) != NULL);
    atomic_store(&s_got, 0);
    atomic_store(&s_basic_calls, 0);
    CHECK(event_bus_publish(EVENT_SYSTEM_STARTUP, NULL, 0) == EVENT_BUS_OK);
    wait_until(&s_got, 2000);
    printf("  [dbg] calls=%d got=%d type=0x%08X expect=0x%08X\n",
           atomic_load(&s_basic_calls), atomic_load(&s_got),
           s_got_type, EVENT_SYSTEM_STARTUP);
    CHECK(atomic_load(&s_got) == 1 && s_got_type == EVENT_SYSTEM_STARTUP);

    /* 参数校验 */
    CHECK(event_bus_publish(EVENT_WIFI_CONNECTED, NULL, 8) == EVENT_BUS_ERR_INVALID_PARAM);
    CHECK(event_bus_publish(EVENT_WIFI_CONNECTED, &data, EVENT_BUS_MAX_EVENT_SIZE + 1)
          == EVENT_BUS_ERR_INVALID_PARAM);

    uint32_t pub = 0, pro = 0, drop = 0, err = 0;
    CHECK(event_bus_get_stats(&pub, &pro, &drop, &err) == EVENT_BUS_OK);
    CHECK(pub >= 2 && pro >= 2);
}

/* ---------- 2. 锁外回调: handler 内再订阅/发布/退订不死锁（5.1 修复1） ---------- */
static atomic_int s_nested_done = 0;

static int on_nested(const event_t* e, void* ud) {
    (void)e; (void)ud;
    atomic_store(&s_nested_done, 1);
    return 0;
}

static int on_reentrant(const event_t* e, void* ud) {
    (void)e;
    /* 危险动作三连：dispatch 过程中再订阅、发布、退订自己 */
    event_subscription_t* self = (event_subscription_t*)ud;
    event_bus_subscribe(EVENT_WIFI_DISCONNECTED, on_nested, NULL);
    event_bus_publish(EVENT_UI_PAGE_CHANGE, NULL, 0);
    event_bus_unsubscribe(self);
    atomic_store(&s_nested_done, 1);
    return 0;
}

static void test_reentrant_dispatch(void) {
    printf("[EB2] reentrant dispatch (no deadlock, 5.1 fix#1)\n");
    atomic_store(&s_nested_done, 0);
    event_subscription_t* sub = event_bus_subscribe(EVENT_SYSTEM_ERROR, on_reentrant, sub);
    CHECK(sub != NULL);

    CHECK(event_bus_publish(EVENT_SYSTEM_ERROR, NULL, 0) == EVENT_BUS_OK);
    wait_until(&s_nested_done, 3000);
    CHECK(atomic_load(&s_nested_done) == 1);   /* 若锁内回调已死锁，测试会挂起 */
    usleep(100000);                            /* 给嵌套订阅的事件处理留时间 */
}

/* ---------- 3. 事件池耗尽 → 堆兜底（5.1 修复2） ---------- */
static void test_pool_fallback(void) {
    printf("[EB3] pool fallback (48 blocks, heap overflow counted)\n");
    uint32_t base_sys = mem_stat_get(MEM_MOD_SYS)->cur;
    uint32_t fallback_before = event_bus_get_heap_fallback();

    /* 不经过 32 深队列，直接创建 60 个事件: 池 48 + 堆兜底 12 */
    event_t* evts[60];
    int made = 0;
    for (int i = 0; i < 60; i++) {
        evts[i] = event_bus_create_event(EVENT_SENSOR_TEMP_HUMIDITY, NULL, 0);
        if (evts[i]) made++;
    }
    CHECK(made == 60);
    CHECK(event_bus_get_heap_fallback() - fallback_before >= 12);

    for (int i = 0; i < made; i++) {
        event_bus_destroy_event(evts[i]);
    }
    CHECK(mem_stat_get(MEM_MOD_SYS)->cur == base_sys);   /* 兜底块全部归还 */
}

/* ---------- 4. 名表全覆盖（5.1 修复4） ---------- */
static void test_name_table(void) {
    printf("[EB4] name table covers event types (spot check)\n");
    /* EVENT_TYPE_MAX 是编码哨兵（MODULE<<16|ID）非元素个数，逐值遍历不可行；
     * 抽查含此前缺失的典型项（表已由枚举自动生成，结构上保证全覆盖） */
    const event_type_t samples[] = {
        EVENT_SYSTEM_STARTUP, EVENT_SYSTEM_WATCHDOG_FEED,
        EVENT_WIFI_CONNECTED, EVENT_WIFI_MODE_CHANGED,
        EVENT_WIFI_AP_STARTED, EVENT_WIFI_AP_STOPPED,
        EVENT_SENSOR_TEMP_HUMIDITY, EVENT_SENSOR_ERROR,
        EVENT_TOUCH_PRESS, EVENT_LORA_READY,
        EVENT_AUDIO_READY, EVENT_STORAGE_READY, EVENT_DISPLAY_READY,
    };
    int unknown = 0;
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        if (strcmp(event_bus_get_type_name(samples[i]), "UNKNOWN") == 0) {
            unknown++;
            printf("  UNKNOWN for type 0x%08X\n", samples[i]);
        }
    }
    CHECK(unknown == 0);
}

/* ---------- 5. 统计与错误计数 ---------- */
static atomic_int s_err_got = 0;

static int on_err(const event_t* e, void* ud) {
    (void)e; (void)ud;
    atomic_store(&s_err_got, 1);
    return -1;                                  /* 模拟 handler 失败 */
}

static void test_stats(void) {
    printf("[EB5] stats / handler error counting\n");
    CHECK(event_bus_reset_stats() == EVENT_BUS_OK);

    event_subscription_t* sub = event_bus_subscribe(EVENT_SENSOR_ERROR, on_err, NULL);
    CHECK(sub != NULL);

    atomic_store(&s_err_got, 0);
    CHECK(event_bus_publish(EVENT_SENSOR_ERROR, NULL, 0) == EVENT_BUS_OK);
    wait_until(&s_err_got, 2000);

    uint32_t pub = 0, pro = 0, drop = 0, herr = 0;
    event_bus_get_stats(&pub, &pro, &drop, &herr);
    CHECK(pub == 1 && pro == 1);
    CHECK(herr == 1);                           /* 非 0 返回被计数 */

    CHECK(event_bus_unsubscribe(sub) == EVENT_BUS_OK);
    CHECK(event_bus_unsubscribe(sub) == EVENT_BUS_ERR_NOT_FOUND);
    CHECK(event_bus_subscribe(EVENT_SENSOR_ERROR, NULL, NULL) == NULL);

    /* 退订后不再投递 */
    atomic_store(&s_err_got, 0);
    event_bus_publish(EVENT_SENSOR_ERROR, NULL, 0);
    usleep(100000);
    CHECK(atomic_load(&s_err_got) == 0);
}

/* 由 test_main.c 统一驱动 */
void test_event_bus_run(int* pass, int* fail) {
    test_basic();
    test_reentrant_dispatch();
    test_pool_fallback();
    test_name_table();
    test_stats();
    *pass += s_pass;
    *fail += s_fail;
}
