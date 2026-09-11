/**
 * @file time_svc.h
 * @brief 契约 I4 时间服务（[HUB] 数据中枢）
 *
 * 本期：开机 uptime 时钟，synced 恒 false（NTP 未接入）。
 * NTP 接口为预留桩（空实现 + TODO），交付后由集成阶段接 sntp。
 */

#ifndef TIME_SVC_H
#define TIME_SVC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TIME_SVC_OK        = 0,
    TIME_SVC_ERR_PARAM = -1,
    TIME_SVC_ERR_FAIL  = -2,
} time_svc_err_t;

typedef struct {
    uint16_t year;
    uint8_t  mon, day;
    uint8_t  hour, min, sec;
    bool     synced;      /* NTP 同步成功标志，本期恒 false */
} time_svc_tm_t;

/**
 * @brief 取当前时间（线程安全；模块未初始化时返回 uptime 时钟）
 */
time_svc_err_t time_svc_get(time_svc_tm_t* out);

/** 开机毫秒数（附加 API，测试/统计用） */
uint64_t time_svc_uptime_ms(void);

/* ---- NTP 预留桩（本期空实现，返回 TIME_SVC_ERR_FAIL 表示未支持） ---- */

/** 预留：启用 SNTP 同步（TODO: 集成阶段接 esp_netif_sntp） */
time_svc_err_t time_svc_ntp_enable(const char* server);

/** 预留：手动设置系统时间（TODO: NTP 批次实现） */
time_svc_err_t time_svc_set(const time_svc_tm_t* tm);

/** 初始化（幂等；app_init optional 注册入口） */
time_svc_err_t time_svc_init(void);

#ifdef __cplusplus
}
#endif

#endif /* TIME_SVC_H */
