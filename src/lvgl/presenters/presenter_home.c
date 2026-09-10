/**
 * @file presenter_home.c
 * @brief 展示器层：主页 presenter 实现
 *
 * 刷新策略（铁律）：LVGL timer 1s 仅刷时间；温湿度/网络变化由 event_bus
 * 事件驱动（handler 在 event_bus 任务上下文，更新 UI 前取 lvgl_app 锁）。
 * 数值一律经 bridge 取（mock/真身同签名）。
 */

#include "presenter_home.h"
#include "page_home.h"
#include "bridge.h"
#include "navigator.h"
#include "lvgl_app.h"
#include "event_bus.h"
#include "logger.h"

static const char* TAG = "[PRE_HOME]";

static lv_timer_t* s_time_timer = NULL;

static void on_settings_clicked(lv_event_t* e);

/* ==================== 时间（1s LVGL timer，仅刷时间） ==================== */

static void time_timer_cb(lv_timer_t* timer) {
    (void)timer;
    ui_bridge_time_t t;
    ui_bridge_time_get(&t);
    page_home_set_time(t.hour, t.min, t.synced);
}

/* ==================== 事件驱动刷新（event_bus → LVGL 锁内更新） ==================== */

static void refresh_from_bridge(void) {
    ui_bridge_sensor_t sensor;
    if (ui_bridge_sensor_get(&sensor)) {
        page_home_set_sensor(sensor.valid, sensor.temp_m_c, sensor.humi_m_p);
    } else {
        page_home_set_sensor(false, 0, 0);
    }

    ui_bridge_net_t net;
    if (ui_bridge_net_get(&net)) {
        page_home_set_net((int)net.mode, net.ssid, net.ip, net.rssi, net.switching);
    } else {
        page_home_set_net(0, "", "", 0, false);
    }
    /* 未读角标占位 0（聊天/通知接入 lora_tp 后由事件驱动） */
    page_home_set_unread(0);
}

static int on_sensor_event(const event_t* e, void* ud) {
    (void)e;
    (void)ud;
    lvgl_app_lock();
    ui_bridge_sensor_t sensor;
    if (ui_bridge_sensor_get(&sensor)) {
        page_home_set_sensor(sensor.valid, sensor.temp_m_c, sensor.humi_m_p);
    } else {
        page_home_set_sensor(false, 0, 0);
    }
    lvgl_app_unlock();
    return 0;
}

static int on_net_event(const event_t* e, void* ud) {
    (void)e;
    (void)ud;
    lvgl_app_lock();
    ui_bridge_net_t net;
    if (ui_bridge_net_get(&net)) {
        page_home_set_net((int)net.mode, net.ssid, net.ip, net.rssi, net.switching);
    }
    lvgl_app_unlock();
    return 0;
}

/* ==================== 生命周期 ==================== */

static void on_page_enter(void) {
    page_home_set_nav_cb(on_settings_clicked);
    refresh_from_bridge();
    time_timer_cb(NULL);   /* 立即渲染一次时间 */
    if (!s_time_timer) {
        s_time_timer = lv_timer_create(time_timer_cb, 1000, NULL);
    }
}

static void on_page_exit(void) {
    if (s_time_timer) {
        lv_timer_del(s_time_timer);
        s_time_timer = NULL;
    }
}

static void on_settings_clicked(lv_event_t* e) {
    (void)e;
    navigator_switch("settings");
}

/* ==================== 初始化 ==================== */

void presenter_home_init(void) {
    static const navigator_page_t page = {
        .id = "home",
        .create = page_home_create,
        .on_enter = on_page_enter,
        .on_exit = on_page_exit,
    };
    navigator_register(&page);

    /* 事件订阅（home 在栈底常驻，handler 内自行判锁；未初始化时返回 NULL 跳过） */
    if (event_bus_subscribe(EVENT_SENSOR_TEMP_HUMIDITY, on_sensor_event, NULL)) {
        LOGI(TAG, "subscribed sensor events");
    } else {
        LOGW(TAG, "sensor event subscribe skipped (bus not ready)");
    }
    if (event_bus_subscribe(EVENT_WIFI_MODE_CHANGED, on_net_event, NULL)) {
        event_bus_subscribe(EVENT_WIFI_GOT_IP, on_net_event, NULL);
        LOGI(TAG, "subscribed net events");
    } else {
        LOGW(TAG, "net event subscribe skipped (bus not ready)");
    }
}
