/**
 * @file web_api_sysinfo.c
 * @brief [WEB] 只读信息路由：传感器快照 / 网络信息 / 时间 / i18n 语言包
 *
 * 契约 I5（docs/three_tasks_plan.md §3）：
 *   GET /api/sensor      {"temp_c":26.5,"humi_p":48.2,"age_ms":1234,"valid":true}
 *   GET /api/net/info    {"mode":"sta","ssid":...,"ip":...,"netmask":...,
 *                         "gw":...,"mac":...,"rssi":-52,"switching":false}
 *   GET /api/time        {"synced":false,"text":"12:34"}
 *   GET /i18n/<lang>.json  原样返回 SPIFFS 上的翻译文件（与板端 LVGL 同源）
 *
 * 数据来源（HUB 交付后接通）：
 *   sensor_snapshot_get()  / components/core/sensor_cache  （契约 I2）
 *   sysinfo_net_get()      / components/modules/sysinfo    （契约 I3）
 *   time_svc_get()         / components/core/time_svc      （契约 I4）
 * 编译开关 WEB_API_USE_HUB（CMakeLists compile_definitions）：
 *   0（默认，HUB 批次①未交付）= 路由内返回 mock JSON 先行；
 *   1 = 调真实接口；届时在该处补 REQUIRES sensor_cache sysinfo time_svc，
 *       若 HUB 错误枚举名与本文假设（SENSOR_OK/SYSINFO_OK/TIME_SVC_OK）
 *       有出入，仅需微调 #if 分支内的判断，不影响路由结构。
 *
 * 本文件为 additive 新增，不改 web.c/web_ota.c 既有逻辑（仅 web.c
 * register_builtin_routes() 末尾追加一行 init 调用，仿 web_ota 模式）。
 */

#include "web.h"
#include "mem.h"
#include "mongoose.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef WEB_API_USE_HUB
#define WEB_API_USE_HUB 0
#endif

#if WEB_API_USE_HUB
#include "sensor_cache.h"
#include "sysinfo.h"
#include "time_svc.h"
#endif

static const char *TAG = "[WEB][SYSINFO]";

/* i18n 文件白名单：仅放行固定语言文件（精确匹配，天然防路径穿越）。
 * HUB 追加新语言时在此 append 一行，并在 assets/i18n/ 同步新增源文件。 */
static const char *const s_i18n_uris[] = {
    "/i18n/zh-CN.json",
    "/i18n/en-US.json",
};
#define WEB_I18N_FILE_MAX (64 * 1024)   /* 语言包大小上限（防御异常文件） */

// ---------------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------------

/** JSON 字符串转义（ ssid 等外部输入防御性处理），out 至少 2*len+1 字节 */
static void json_escape(char *out, size_t outlen, const char *src) {
    size_t o = 0;
    for (const char *p = src; *p && o + 2 < outlen; p++) {
        if (*p == '"' || *p == '\\') out[o++] = '\\';
        out[o++] = *p;
    }
    out[o] = '\0';
}

static void reply_json(struct mg_connection *c, const char *body) {
    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", body);
}

// ---------------------------------------------------------------------------
// GET /api/sensor —— 契约 I2 快照（HUB 未交付期间 mock）
// ---------------------------------------------------------------------------

static void handle_sensor(struct mg_connection *c, struct mg_http_message *hm) {
    (void)hm;
    char body[128];

#if WEB_API_USE_HUB
    sensor_snapshot_t snap;
    if (sensor_snapshot_get(&snap) != SENSOR_OK) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"sensor cache unavailable\"}");
        return;
    }
    if (!snap.valid) {
        /* 无有效采样：valid=false，值域置 0，前端显示"暂无数据" */
        reply_json(c, "{\"temp_c\":0,\"humi_p\":0,\"age_ms\":0,\"valid\":false}");
        return;
    }
    /* 毫单位 → 一位小数（契约要求后端换算） */
    snprintf(body, sizeof(body),
             "{\"temp_c\":%.1f,\"humi_p\":%.1f,\"age_ms\":%lu,\"valid\":true}",
             (double)snap.temp_m_c / 1000.0,
             (double)snap.humi_m_p / 1000.0,
             (unsigned long)snap.age_ms);
    reply_json(c, body);
#else
    /* MOCK：HUB 批次①未交付，固定示例值先行（前端联调用） */
    (void)body;
    reply_json(c,
               "{\"temp_c\":26.5,\"humi_p\":48.2,\"age_ms\":1234,\"valid\":true}");
#endif
}

// ---------------------------------------------------------------------------
// GET /api/net/info —— 契约 I3（HUB 未交付期间 mock）
// ---------------------------------------------------------------------------

static void handle_net_info(struct mg_connection *c, struct mg_http_message *hm) {
    (void)hm;

#if WEB_API_USE_HUB
    net_info_t info;
    if (sysinfo_net_get(&info) != SYSINFO_OK) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"sysinfo unavailable\"}");
        return;
    }
    const char *mode = (info.mode == NET_MODE_STA) ? "sta"
                     : (info.mode == NET_MODE_AP)  ? "ap" : "off";
    char ssid[67];
    json_escape(ssid, sizeof(ssid), info.ssid);
    char body[256];
    snprintf(body, sizeof(body),
             "{\"mode\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\","
             "\"netmask\":\"%s\",\"gw\":\"%s\",\"mac\":\"%s\","
             "\"rssi\":%d,\"switching\":%s}",
             mode, ssid, info.ip, info.netmask, info.gw, info.mac,
             (int)info.rssi, info.switching ? "true" : "false");
    reply_json(c, body);
#else
    /* MOCK：AP 模式默认态 */
    reply_json(c,
               "{\"mode\":\"ap\",\"ssid\":\"OVS-Desk\",\"ip\":\"192.168.4.1\","
               "\"netmask\":\"255.255.255.0\",\"gw\":\"192.168.4.1\","
               "\"mac\":\"7C:DF:A1:9F:3B:E2\",\"rssi\":-52,\"switching\":false}");
#endif
}

// ---------------------------------------------------------------------------
// GET /api/time —— 契约 I4（HUB 未交付期间用系统时钟 mock）
// ---------------------------------------------------------------------------

static void handle_time(struct mg_connection *c, struct mg_http_message *hm) {
    (void)hm;
    char body[64];

#if WEB_API_USE_HUB
    time_svc_tm_t tm;
    if (time_svc_get(&tm) != TIME_SVC_OK) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"time svc unavailable\"}");
        return;
    }
    snprintf(body, sizeof(body),
             "{\"synced\":%s,\"text\":\"%02u:%02u\"}",
             tm.synced ? "true" : "false",
             (unsigned)tm.hour, (unsigned)tm.min);
    reply_json(c, body);
#else
    /* MOCK：设备本地时钟（未接 NTP 时可能非真实时刻，仅验证链路） */
    time_t now = time(NULL);
    struct tm lt;
#if defined(_WIN32)
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);   /* ESP-IDF newlib / POSIX */
#endif
    snprintf(body, sizeof(body),
             "{\"synced\":false,\"text\":\"%02d:%02d\"}", lt.tm_hour, lt.tm_min);
    reply_json(c, body);
#endif
}

// ---------------------------------------------------------------------------
// GET /i18n/<lang>.json —— SPIFFS 白名单读取（与板端 LVGL 共用翻译源）
// ---------------------------------------------------------------------------

static void handle_i18n_file(struct mg_connection *c, struct mg_http_message *hm) {
    char uri[64];
    snprintf(uri, sizeof(uri), "%.*s", (int)hm->uri.len, hm->uri.buf);

    /* 双重防御：路由本就精确注册白名单 uri，此处再校验一次 */
    bool allowed = false;
    for (size_t i = 0; i < sizeof(s_i18n_uris) / sizeof(s_i18n_uris[0]); i++) {
        if (strcmp(uri, s_i18n_uris[i]) == 0) { allowed = true; break; }
    }
    if (!allowed) {
        mg_http_reply(c, 404, "Content-Type: application/json\r\n",
                      "{\"error\":\"i18n pack not found\"}");
        return;
    }

    char path[96];
    snprintf(path, sizeof(path), "%s%s", WEB_SPIFFS_MOUNT, uri);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        /* HUB 尚未打包翻译资源到 /i18n/ 时到达此处，前端回退内置语言包 */
        LOGW(TAG, "i18n file not deployed: %s", path);
        mg_http_reply(c, 404, "Content-Type: application/json\r\n",
                      "{\"error\":\"i18n pack not found\"}");
        return;
    }

    char *buf = NULL;
    if (fseek(fp, 0, SEEK_END) != 0) goto fail;
    long size = ftell(fp);
    if (size <= 0 || size > WEB_I18N_FILE_MAX) goto fail;
    rewind(fp);

    buf = (char *)mem_malloc((size_t)size);
    if (!buf) {
        LOGE(TAG, "oom for i18n file (%ld B)", size);
        goto fail;
    }
    if (fread(buf, 1, (size_t)size, fp) != (size_t)size) goto fail;
    fclose(fp);
    fp = NULL;

    mg_http_reply(c, 200,
                  "Content-Type: application/json\r\n"
                  "Cache-Control: no-cache\r\n",
                  "%.*s", (int)size, buf);
    mem_free(buf);
    return;

fail:
    if (fp) fclose(fp);
    mem_free(buf);
    mg_http_reply(c, 404, "Content-Type: application/json\r\n",
                  "{\"error\":\"i18n pack not found\"}");
}

// ---------------------------------------------------------------------------
// 路由注册（web.c register_builtin_routes() 追加调用）
// ---------------------------------------------------------------------------

int web_api_sysinfo_init(void) {
    web_route_t r;

    r = (web_route_t){"GET", "/api/sensor", handle_sensor};
    if (web_register_route(&r) != 0) return -1;
    r = (web_route_t){"GET", "/api/net/info", handle_net_info};
    if (web_register_route(&r) != 0) return -1;
    r = (web_route_t){"GET", "/api/time", handle_time};
    if (web_register_route(&r) != 0) return -1;
    for (size_t i = 0; i < sizeof(s_i18n_uris) / sizeof(s_i18n_uris[0]); i++) {
        r = (web_route_t){"GET", s_i18n_uris[i], handle_i18n_file};
        if (web_register_route(&r) != 0) return -1;
    }

    LOGI(TAG, "sysinfo routes registered (%d i18n langs)",
         (int)(sizeof(s_i18n_uris) / sizeof(s_i18n_uris[0])));
    return 0;
}
