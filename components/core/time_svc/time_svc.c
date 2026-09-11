/**
 * @file time_svc.c
 * @brief 契约 I4 时间服务实现（uptime 时钟 + NTP 桩）
 *
 * 本期 synced 恒 false：UI 照常显示时间但灰显（bridge/pages 契约）。
 */

#include "time_svc.h"
#include "time_svc_port.h"
#include "logger.h"

static const char* TAG = "[TIME_SVC]";

static bool s_inited = false;
/* 预留：NTP 接入后的 epoch 基准（秒）与锚点 */
static uint64_t s_epoch_base_ms = 0;   /* 0 = 无绝对时间，纯 uptime */

time_svc_err_t time_svc_init(void) {
    if (s_inited) {
        return TIME_SVC_OK;
    }
    s_inited = true;
    LOGI(TAG, "time service ready (uptime clock, NTP not synced)");
    return TIME_SVC_OK;
}

uint64_t time_svc_uptime_ms(void) {
    return time_svc_port_tick_ms();
}

time_svc_err_t time_svc_get(time_svc_tm_t* out) {
    if (!out) {
        return TIME_SVC_ERR_PARAM;
    }
    uint64_t total_sec = time_svc_port_tick_ms() / 1000ULL;

    if (s_epoch_base_ms > 0) {
        /* TODO(NTP): 有 epoch 基准时换算真实日期 */
        total_sec += s_epoch_base_ms / 1000ULL;
    }

    out->sec = (uint8_t)(total_sec % 60ULL);
    out->min = (uint8_t)((total_sec / 60ULL) % 60ULL);
    out->hour = (uint8_t)((total_sec / 3600ULL) % 24ULL);
    /* 无 NTP：日期字段填固定缺省（UI 本期不消费日期） */
    out->year = 2026;
    out->mon = 1;
    out->day = 1;
    out->synced = false;
    return TIME_SVC_OK;
}

time_svc_err_t time_svc_ntp_enable(const char* server) {
    (void)server;
    LOGW(TAG, "NTP not supported yet (stub)");
    return TIME_SVC_ERR_FAIL;
}

time_svc_err_t time_svc_set(const time_svc_tm_t* tm) {
    (void)tm;
    LOGW(TAG, "manual time set not supported yet (stub)");
    return TIME_SVC_ERR_FAIL;
}
