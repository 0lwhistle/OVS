/**
 * @brief 心跳任务实现
 * 
 * 每秒通过 tasker 调度一次，缓存 WiFi 信号强度和运行时间。
 * web 层通过 heartbeat_get_* 接口读取，避免每次 HTTP 请求都执行 Wi-Fi 查询。
 */
#include "heartbeat.h"
#include "tasker.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "logger.h"

static const char *TAG = "[HEARTBEAT]";

// ---------- 缓存数据 ----------
static int s_cached_rssi = 0;
static uint64_t s_start_time_us = 0;

// ---------- tasker 回调函数 ----------
static enum task_t heartbeat_task_fn(void *ctx) {
    (void)ctx;

    // 1. 更新 WiFi 信号强度（esp_wifi_sta_get_ap_info 是轻量调用）
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        s_cached_rssi = ap_info.rssi;
    }

    return TASK_OK;
}

// ---------- 初始化 ----------
int heartbeat_init(void) {
    // 记录启动时间
    s_start_time_us = esp_timer_get_time();

    // 注册到 tasker 系统：中等优先级，周期 1000ms，无限运行
    struct task_node node;
    int ret = tasker_task_init_mi(
        &node,
        1000,       // period = 1000ms (1秒)
        -1,         // run_cnt = -1 (无限)
        "heartbeat",
        heartbeat_task_fn,
        NULL        // ctx = NULL
    );
    if (ret != TASK_OK) {
        LOGE(TAG, "Failed to init heartbeat task node");
        return -1;
    }

    // 立即执行一次，让缓存有初始值
    heartbeat_task_fn(NULL);

    // 注册到 tasker 调度系统
    ret = tasker_enqueue(&node);
    if (ret != TASK_OK) {
        LOGE(TAG, "Failed to enqueue heartbeat task");
        return -1;
    }

    LOGI(TAG, "Heartbeat task registered (period=1000ms)");
    return 0;
}

// ---------- 读取缓存数据 ----------
int heartbeat_get_rssi(void) {
    return s_cached_rssi;
}

uint64_t heartbeat_get_uptime_sec(void) {
    uint64_t now = esp_timer_get_time();
    uint64_t elapsed_us = now - s_start_time_us;
    return elapsed_us / 1000000;
}