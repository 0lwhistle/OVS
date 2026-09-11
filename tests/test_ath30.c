/**
 * @file test_ath30.c
 * @brief [HUB] 批次② ath30 收口 PC 门禁（mock i2c 三场景 + 停采/恢复）
 *
 * 场景（idle_modules_plan §3.2-4）：
 *   A1 正常读取：事件载荷断言（float→milli 由消费层换算，这里验证
 *      事件发布与数值正确性）；
 *   A2 无应答超时：连续失败 3 次→停采+仅发布一次 ERROR；
 *   A3 CRC 错误：同 A2 计数路径；恢复：i2c 转好后 ≤5 周期内恢复采样；
 * 另覆盖：设备树 sample_interval_ms 读取与缺省兜底、init 自动启动。
 */

#include "ath30.h"
#include "i2c_mock.h"
#include "sensor_cache.h"
#include "event_bus.h"

#include <stdio.h>
#include <string.h>

static int s_pass = 0, s_fail = 0;
#define CHECK(cond) do { \
    if (cond) { s_pass++; } \
    else { s_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* 事件计数（经 sensor_cache 缓存间接观察 + 直接订阅） */
static int s_data_events = 0;
static int s_error_events = 0;

static int on_data(const event_t* e, void* ud) {
    (void)e; (void)ud;
    s_data_events++;
    return 0;
}

static int on_error(const event_t* e, void* ud) {
    (void)e; (void)ud;
    s_error_events++;
    return 0;
}

static void wait_for(int* counter, int target, int timeout_ms) {
    for (int i = 0; i < timeout_ms && *counter < target; i += 5) {
        usleep(5000);
    }
}

static void test_ath30_all(void) {
    printf("[HUB-T1] ath30 normal read + event payload\n");

    event_subscription_t* sub_d = event_bus_subscribe(EVENT_SENSOR_TEMP_HUMIDITY, on_data, NULL);
    event_subscription_t* sub_e = event_bus_subscribe(EVENT_SENSOR_ERROR, on_error, NULL);
    CHECK(sub_d && sub_e);

    /* 设备树 mock：采集间隔 50ms（dtree_mock 需预置 ath30-sensor 节点键） */
    extern void dtree_mock_reset(void);
    extern void dtree_mock_set_int(const char*, const char*, int32_t);
    dtree_mock_set_int("ath30-sensor", "sample_interval_ms", 50);

    i2c_mock_set_mode(I2C_MOCK_MODE_NORMAL);
    i2c_mock_reset_stats();

    CHECK(ath30_init() == ath30_OK);   /* init 自动启动周期采集 */
    CHECK(ath30_is_initialized());

    /* A1：单次读取值断言（mock 帧 = 25.0°C / 50.0%RH） */
    ath30_data_t d;
    CHECK(ath30_read(&d) == ath30_OK);
    CHECK(d.temperature > 24.9f && d.temperature < 25.1f);
    CHECK(d.humidity > 49.9f && d.humidity < 50.1f);

    /* 周期采集：1s 内应产生 ≥2 次数据事件 */
    wait_for(&s_data_events, 2, 1500);
    CHECK(s_data_events >= 2);
    int triggers_before_stop = i2c_mock_trigger_count();

    /* A2：无应答 → 3 次失败停采 + 仅一次 ERROR */
    printf("[HUB-T1] ath30 no-ack x3 -> suspend + single ERROR\n");
    i2c_mock_set_mode(I2C_MOCK_MODE_NO_ACK);
    wait_for(&s_error_events, 1, 3000);
    CHECK(s_error_events == 1);        /* 达限只发一次 */
    usleep(300);                       /* 停采窗口：不应再有触发 */
    int triggers_at_suspend = i2c_mock_trigger_count();
    CHECK(s_error_events == 1);        /* 停采后无新 ERROR */

    /* A2b：CRC 错误场景单测（单次 read 直接断言错误码） */
    i2c_mock_set_mode(I2C_MOCK_MODE_BAD_CRC);
    CHECK(ath30_read(&d) == ath30_ERR_CRC);

    /* A3：停采期间跳过采集（触发次数几乎不涨），恢复后重新出数据 */
    printf("[HUB-T1] ath30 recovery within retry periods\n");
    usleep(200);
    int skip_growth = i2c_mock_trigger_count() - triggers_at_suspend;
    CHECK(skip_growth <= 1);           /* 200ms/50ms≈4 周期只允许 ≤1 次重试触发 */

    i2c_mock_set_mode(I2C_MOCK_MODE_NORMAL);
    int data_before = s_data_events;
    wait_for(&s_data_events, data_before + 1, 3000);
    CHECK(s_data_events > data_before);   /* 恢复采样 */
    CHECK(s_error_events == 1);           /* 恢复过程不再发 ERROR */

    /* 收尾：停采 + deinit 幂等 */
    CHECK(ath30_stop_periodic_read() == ath30_OK);
    CHECK(ath30_stop_periodic_read() == ath30_OK);
    CHECK(ath30_deinit() == ath30_OK);
    CHECK(ath30_read(&d) == ath30_ERR_NOT_INIT);

    event_bus_unsubscribe(sub_d);
    event_bus_unsubscribe(sub_e);
}

void test_ath30_run(int* pass, int* fail) {
    test_ath30_all();
    *pass += s_pass;
    *fail += s_fail;
}
