/**
 * @file sensor_cache.c
 * @brief 契约 I2 传感器快照缓存实现（event_bus 订阅 → 线程安全快照）
 */

#include "sensor_cache.h"
#include "sensor_cache_port.h"
#include "event_bus.h"
#include "logger.h"

static const char* TAG = "[SENSOR_CACHE]";

typedef struct {
    sensor_snapshot_t snap;      /* age_ms 由 get 时换算 */
    uint64_t last_update_ms;     /* 单调时基锚点 */
    bool has_sample;
} sensor_cache_state_t;

static sensor_cache_state_t s_state;
static sensor_lock_t* s_lock = NULL;
static event_subscription_t* s_sub_data = NULL;
static event_subscription_t* s_sub_err = NULL;

static int on_sensor_data(const event_t* event, void* user_data) {
    (void)user_data;
    const event_sensor_temp_humidity_t* d = (const event_sensor_temp_humidity_t*)event->data;
    if (!d || event->header.data_len < sizeof(*d)) {
        LOGW(TAG, "malformed sensor payload (len=%u)", (unsigned)event->header.data_len);
        return 0;
    }
    if (s_lock && !sensor_lock_take(s_lock, 100)) {
        LOGW(TAG, "lock timeout, drop sample");
        return 0;
    }
    s_state.snap.temp_m_c = (int32_t)(d->temperature * 1000.0f + (d->temperature >= 0 ? 0.5f : -0.5f));
    s_state.snap.humi_m_p = (int32_t)(d->humidity * 1000.0f + 0.5f);
    s_state.snap.valid = true;
    s_state.last_update_ms = sensor_tick_ms();
    s_state.has_sample = true;
    if (s_lock) {
        sensor_lock_give(s_lock);
    }
    LOGD(TAG, "cached: temp=%ld mC humi=%ld mP",
         (long)s_state.snap.temp_m_c, (long)s_state.snap.humi_m_p);
    return 0;
}

static int on_sensor_error(const event_t* event, void* user_data) {
    (void)event;
    (void)user_data;
    if (s_lock && !sensor_lock_take(s_lock, 100)) {
        return 0;
    }
    /* 传感器报错：数据视为失效（保留最后值供调试，valid=false） */
    if (s_state.has_sample) {
        LOGW(TAG, "sensor error, mark snapshot invalid");
    }
    s_state.snap.valid = false;
    if (s_lock) {
        sensor_lock_give(s_lock);
    }
    return 0;
}

sensor_err_t sensor_cache_init(void) {
    if (s_lock) {
        return SENSOR_OK;   /* 幂等 */
    }
    s_lock = sensor_lock_create();
    if (!s_lock) {
        LOGE(TAG, "lock create failed");
        return SENSOR_ERR_FAIL;
    }
    memset(&s_state, 0, sizeof(s_state));

    s_sub_data = event_bus_subscribe(EVENT_SENSOR_TEMP_HUMIDITY, on_sensor_data, NULL);
    s_sub_err = event_bus_subscribe(EVENT_SENSOR_ERROR, on_sensor_error, NULL);
    if (!s_sub_data || !s_sub_err) {
        LOGE(TAG, "subscribe failed (data=%p err=%p)", (void*)s_sub_data, (void*)s_sub_err);
        sensor_cache_deinit();
        return SENSOR_ERR_FAIL;
    }
    LOGI(TAG, "sensor cache ready");
    return SENSOR_OK;
}

sensor_err_t sensor_snapshot_get(sensor_snapshot_t* out) {
    if (!out) {
        return SENSOR_ERR_PARAM;
    }
    memset(out, 0, sizeof(*out));
    if (!s_lock) {
        return SENSOR_ERR_FAIL;
    }
    if (!sensor_lock_take(s_lock, 100)) {
        return SENSOR_ERR_FAIL;
    }
    if (s_state.has_sample) {
        *out = s_state.snap;
        uint64_t now = sensor_tick_ms();
        out->age_ms = (now > s_state.last_update_ms)
                          ? (uint32_t)(now - s_state.last_update_ms)
                          : 0;
    }
    sensor_lock_give(s_lock);
    return out->valid ? SENSOR_OK : SENSOR_ERR_FAIL;
}

void sensor_cache_deinit(void) {
    if (s_sub_data) {
        event_bus_unsubscribe(s_sub_data);
        s_sub_data = NULL;
    }
    if (s_sub_err) {
        event_bus_unsubscribe(s_sub_err);
        s_sub_err = NULL;
    }
    if (s_lock) {
        sensor_lock_destroy(s_lock);
        s_lock = NULL;
    }
}
