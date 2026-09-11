/**
 * @file test_hub.c
 * @brief [HUB] 批次①统一数据接口 PC 门禁（契约 I1~I4）
 *
 * I1 i18n       —— 真实实现 + 临时目录语言包（加载/切换/回退/节流）
 * I2 sensor_cache —— 真实实现 + event_bus 注入（数据/错误事件→快照）
 * I4 time_svc   —— 真实实现（uptime 时钟/synced=false/NTP 桩）
 * I3 sysinfo    —— 真实实现 + net_mgr_mock + PC port stub
 */

#include "i18n.h"
#include "sensor_cache.h"
#include "time_svc.h"
#include "sysinfo.h"
#include "event_bus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int s_pass = 0, s_fail = 0;
#define CHECK(cond) do { \
    if (cond) { s_pass++; } \
    else { s_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ------------------------------------------------------------------ */
/* i18n 测试语言包目录                                                  */
/* ------------------------------------------------------------------ */

static void write_file(const char* path, const char* content) {
    FILE* fp = fopen(path, "wb");
    if (fp) {
        fputs(content, fp);
        fclose(fp);
    }
}

static void make_i18n_fixture(const char* dir) {
    mkdir(dir, 0755);
    char path[256];

    snprintf(path, sizeof(path), "%s/zh-CN.json", dir);
    write_file(path,
        "{\"lang\":\"zh-CN\",\"ver\":2,\"strings\":{"
        "\"APP_TITLE\":\"OVS 助手\",\"HOME_TEMP\":\"温度\","
        "\"BAD_VALUE\":123}}");

    snprintf(path, sizeof(path), "%s/en-US.json", dir);
    write_file(path,
        "{\"lang\":\"en-US\",\"ver\":2,\"strings\":{"
        "\"APP_TITLE\":\"OVS Assistant\",\"HOME_TEMP\":\"Temp\"}}");

    snprintf(path, sizeof(path), "%s/broken.json", dir);
    write_file(path, "{\"lang\":");   /* 非法 JSON */
}

static void test_i18n(void) {
    printf("[HUB-I1] i18n load/switch/fallback\n");
    const char* dir = "/tmp/ovs_tests_i18n";
    make_i18n_fixture(dir);

    i18n_set_base_path(dir);

    /* 未初始化：回退原文 */
    CHECK(strcmp(i18n_get("APP_TITLE"), "APP_TITLE") == 0);
    CHECK(strcmp(i18n_current(), "") == 0);
    CHECK(!i18n_is_loaded());

    /* 载入 zh-CN */
    CHECK(i18n_init("zh-CN") == I18N_OK);
    CHECK(i18n_is_loaded());
    CHECK(strcmp(i18n_current(), "zh-CN") == 0);
    CHECK(strcmp(i18n_get("APP_TITLE"), "OVS 助手") == 0);
    CHECK(strcmp(i18n_get("HOME_TEMP"), "温度") == 0);

    /* 未命中回退原文（节流：重复 miss 不刷屏——这里只验证语义） */
    CHECK(strcmp(i18n_get("NOT_EXIST"), "NOT_EXIST") == 0);
    CHECK(strcmp(i18n_get("NOT_EXIST"), "NOT_EXIST") == 0);

    /* 非法值跳过，不崩 */
    CHECK(strcmp(i18n_get("BAD_VALUE"), "BAD_VALUE") == 0);

    /* 热切换 */
    CHECK(i18n_set_language("en-US") == I18N_OK);
    CHECK(strcmp(i18n_current(), "en-US") == 0);
    CHECK(strcmp(i18n_get("APP_TITLE"), "OVS Assistant") == 0);

    /* 非法参数 */
    CHECK(i18n_set_language(NULL) == I18N_ERR_PARAM);
    CHECK(i18n_set_language("") == I18N_ERR_PARAM);
    CHECK(strcmp(i18n_current(), "en-US") == 0);   /* 失败保持原语言 */

    /* 坏语言包：切换失败保持原语言 */
    CHECK(i18n_set_language("broken") == I18N_ERR_FORMAT);
    CHECK(strcmp(i18n_current(), "en-US") == 0);
    CHECK(strcmp(i18n_get("APP_TITLE"), "OVS Assistant") == 0);

    /* 缺失语言包：IO 错误 */
    CHECK(i18n_set_language("fr-FR") == I18N_ERR_IO);

    /* deinit 后回退原文，可重新 init */
    i18n_deinit();
    CHECK(!i18n_is_loaded());
    CHECK(strcmp(i18n_get("APP_TITLE"), "APP_TITLE") == 0);
    CHECK(i18n_init("zh-CN") == I18N_OK);
    CHECK(strcmp(i18n_get("APP_TITLE"), "OVS 助手") == 0);

    /* NULL label 防御 */
    CHECK(strcmp(i18n_get(NULL), "") == 0);
}

/* ------------------------------------------------------------------ */
/* sensor_cache（event_bus 注入）                                       */
/* ------------------------------------------------------------------ */

static bool wait_snapshot(sensor_snapshot_t* out, int timeout_ms) {
    for (int i = 0; i < timeout_ms; i += 2) {
        if (sensor_snapshot_get(out) == SENSOR_OK) {
            return true;
        }
        usleep(2000);
    }
    sensor_snapshot_get(out);
    return false;
}

static void test_sensor_cache(void) {
    printf("[HUB-I2] sensor_cache event inject\n");

    CHECK(sensor_cache_init() == SENSOR_OK);
    CHECK(sensor_cache_init() == SENSOR_OK);   /* 幂等 */

    /* 无数据：ERR 且 valid=false */
    sensor_snapshot_t snap;
    CHECK(sensor_snapshot_get(&snap) == SENSOR_ERR_FAIL);
    CHECK(!snap.valid);

    /* 注入数据事件（float 载荷 → milli 快照） */
    event_sensor_temp_humidity_t d = {.temperature = 26.5f, .humidity = 48.25f, .timestamp = 0};
    CHECK(event_bus_publish(EVENT_SENSOR_TEMP_HUMIDITY, &d, sizeof(d)) == EVENT_BUS_OK);
    CHECK(wait_snapshot(&snap, 1000));
    CHECK(snap.valid);
    CHECK(snap.temp_m_c == 26500);
    CHECK(snap.humi_m_p == 48250);
    CHECK(snap.age_ms < 1000);

    /* 更新：取新值，age 归零重计 */
    usleep(30000);   /* 30ms，保证 age 可观测 */
    d.temperature = 25.0f;
    d.humidity = 50.0f;
    CHECK(event_bus_publish(EVENT_SENSOR_TEMP_HUMIDITY, &d, sizeof(d)) == EVENT_BUS_OK);
    {
        bool got = false;   /* 异步派发：轮询直到快照翻到新值 */
        for (int i = 0; i < 1000 && !got; i += 2) {
            sensor_snapshot_get(&snap);
            got = (snap.valid && snap.temp_m_c == 25000 && snap.humi_m_p == 50000);
            if (!got) usleep(2000);
        }
        CHECK(got);
        CHECK(snap.age_ms < 1000);
    }

    /* 错误事件：快照失效 */
    CHECK(event_bus_publish(EVENT_SENSOR_ERROR, NULL, 0) == EVENT_BUS_OK);
    {
        bool invalidated = false;
        for (int i = 0; i < 500 && !invalidated; i += 2) {
            sensor_snapshot_get(&snap);
            invalidated = !snap.valid;
            usleep(2000);
        }
        CHECK(invalidated);
        CHECK(sensor_snapshot_get(&snap) == SENSOR_ERR_FAIL);
    }

    /* 坏载荷：不崩、快照保持旧状态 */
    CHECK(event_bus_publish(EVENT_SENSOR_TEMP_HUMIDITY, NULL, 0) == EVENT_BUS_OK);
    usleep(50000);
    CHECK(sensor_snapshot_get(&snap) == SENSOR_ERR_FAIL);   /* 仍无效（错误后未再收数据） */

    sensor_cache_deinit();
    CHECK(sensor_snapshot_get(&snap) == SENSOR_ERR_FAIL);   /* 去初始化后降级 */
    CHECK(sensor_snapshot_get(NULL) == SENSOR_ERR_PARAM);
}

/* ------------------------------------------------------------------ */
/* time_svc                                                            */
/* ------------------------------------------------------------------ */

static void test_time_svc(void) {
    printf("[HUB-I4] time_svc uptime clock\n");

    CHECK(time_svc_init() == TIME_SVC_OK);
    CHECK(time_svc_init() == TIME_SVC_OK);   /* 幂等 */

    time_svc_tm_t tm;
    CHECK(time_svc_get(&tm) == TIME_SVC_OK);
    CHECK(!tm.synced);                        /* 本期恒 false */
    CHECK(tm.hour < 24 && tm.min < 60 && tm.sec < 60);

    /* 单调推进：睡 30ms 后 uptime 至少 +25ms */
    uint64_t t0 = time_svc_uptime_ms();
    usleep(30000);
    CHECK(time_svc_uptime_ms() >= t0 + 25);

    /* 参数防御 */
    CHECK(time_svc_get(NULL) == TIME_SVC_ERR_PARAM);

    /* NTP 桩：本期明确返回不支持 */
    CHECK(time_svc_ntp_enable("pool.ntp.org") == TIME_SVC_ERR_FAIL);
    CHECK(time_svc_set(&tm) == TIME_SVC_ERR_FAIL);
}

/* ------------------------------------------------------------------ */
/* sysinfo（net_mgr_mock + PC port stub）                               */
/* ------------------------------------------------------------------ */

void net_mgr_mock_set_online(bool online);

static void test_sysinfo(void) {
    printf("[HUB-I3] sysinfo net/dev query\n");

    CHECK(sysinfo_init() == SYSINFO_OK);
    CHECK(sysinfo_init() == SYSINFO_OK);   /* 幂等 */

    CHECK(sysinfo_device_get(NULL) == SYSINFO_ERR_PARAM);
    CHECK(sysinfo_net_get(NULL) == SYSINFO_ERR_PARAM);

    /* STA 在线：net_mgr mock + port stub */
    net_mgr_mock_set_online(true);
    net_info_t info;
    CHECK(sysinfo_net_get(&info) == SYSINFO_OK);
    CHECK(info.mode == NET_MODE_STA);
    CHECK(!info.switching);
    CHECK(strcmp(info.ssid, "wifi2.4g") == 0);
    CHECK(strcmp(info.ip, "192.168.2.111") == 0);
    CHECK(strcmp(info.netmask, "255.255.255.0") == 0);
    CHECK(strcmp(info.gw, "192.168.2.1") == 0);
    CHECK(strcmp(info.mac, "AA:BB:CC:DD:EE:FF") == 0);
    CHECK(info.rssi == -52);

    /* AP 模式：rssi 归 0，取 AP 接口 */
    net_mgr_mock_set_online(false);
    CHECK(sysinfo_net_get(&info) == SYSINFO_OK);
    CHECK(info.mode == NET_MODE_AP);
    CHECK(info.rssi == 0);
    CHECK(strcmp(info.ssid, "OVS-Desk") == 0);

    /* 设备信息 */
    sysinfo_dev_t dev;
    CHECK(sysinfo_device_get(&dev) == SYSINFO_OK);
    CHECK(dev.fw_version[0] != '\0');
}

/* ------------------------------------------------------------------ */

void test_hub_run(int* pass, int* fail) {
    /* 本文件依赖 event_bus 已初始化（test_event_bus 先行运行） */
    test_i18n();
    i18n_deinit();   /* 清理全局状态，避免串扰 */
    test_sensor_cache();
    test_time_svc();
    test_sysinfo();
    *pass += s_pass;
    *fail += s_fail;
}
