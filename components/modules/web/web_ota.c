/**
 * @file web_ota.c
 * @brief Web 模块内置的 OTA 端点
 *
 * 端点：
 *   POST /api/ota/firmware   流式固件上传（推荐）
 *   POST /ota/update         同上（旧脚本路径兼容）
 *   GET  /api/ota/status     升级状态（进度/SHA256/槽位/版本）
 *   GET  /ota/progress       同上（旧脚本路径兼容）
 *
 * 上传基于 web 模块 STREAM 路由：on_hdrs 校验并 ota_begin，
 * on_data 分块 ota_write（内存占用恒定），收齐后 ota_end 校验并重启。
 */

#include "web_ota.h"
#include "web.h"
#include "ota.h"
#include "mongoose.h"
#include "logger.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "[WEB][OTA]";

/* 分区表 ota_0/ota_1 各 0x280000 (2.5MB)，留少量余量 */
#define OTA_UPLOAD_MAX_SIZE  (0x280000u)

/* 单上传会话状态（web 任务单线程访问） */
static bool s_active = false;
static size_t s_expected = 0;
static size_t s_received = 0;

static void reply_error(struct mg_connection *c, int code, const char *msg) {
    mg_printf(c,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/json\r\n"
              "Connection: close\r\n"
              "Content-Length: %d\r\n"
              "\r\n"
              "{\"status\":\"error\",\"error\":\"%s\"}",
              code, code == 413 ? "Payload Too Large" :
                    code == 405 ? "Method Not Allowed" : "Bad Request",
              (int)(strlen(msg) + 40), msg);
    c->is_draining = 1;
}

static void reply_ok_and_reboot(struct mg_connection *c, const char *sha_hex) {
    char body[160];
    int blen = snprintf(body, sizeof(body),
                        "{\"status\":\"ok\",\"sha256\":\"%s\","
                        "\"bytes\":%u,\"message\":\"verified, rebooting\"}",
                        sha_hex, (unsigned)s_received);

    mg_printf(c,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json\r\n"
              "Connection: close\r\n"
              "Content-Length: %d\r\n"
              "\r\n"
              "%.*s",
              blen, blen, body);
    c->is_draining = 1;
}

static void sha_to_hex(const uint8_t sha[32], char out[65]) {
    static const char hexd[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2] = hexd[sha[i] >> 4];
        out[i * 2 + 1] = hexd[sha[i] & 0xF];
    }
    out[64] = '\0';
}

// ---------- STREAM: on_hdrs ----------
static int ota_upload_on_hdrs(struct mg_connection *c,
                              struct mg_http_message *hm) {
    if (s_active) {
        reply_error(c, 409, "upload already in progress");
        return -1;
    }

    struct mg_str *cl = mg_http_get_header(hm, "Content-Length");
    if (!cl || cl->len == 0 || cl->len > 12) {
        reply_error(c, 400, "missing Content-Length");
        return -1;
    }
    char clbuf[16] = {0};
    memcpy(clbuf, cl->buf, cl->len);
    long long total = atoll(clbuf);
    if (total <= 0) {
        reply_error(c, 400, "invalid Content-Length");
        return -1;
    }
    if ((unsigned long long)total > OTA_UPLOAD_MAX_SIZE) {
        reply_error(c, 413, "firmware exceeds OTA partition size");
        return -1;
    }

    ota_err_t err = ota_begin((size_t)total);
    if (err != OTA_OK) {
        reply_error(c, 503, ota_err_to_str(err));
        return -1;
    }

    s_active = true;
    s_expected = (size_t)total;
    s_received = 0;
    LOGI(TAG, "Upload started: %lld bytes", total);
    return 0;   /* 接管连接 */
}

// ---------- STREAM: on_data ----------
static size_t ota_upload_on_data(struct mg_connection *c,
                                 const char *data, size_t len) {
    (void)c;
    size_t remaining = s_expected - s_received;
    size_t take = (len < remaining) ? len : remaining;
    if (take == 0) {
        LOGE(TAG, "Unexpected extra data (%zu bytes)", len);
        return (size_t)-1;
    }

    ota_err_t err = ota_write(data, take);
    if (err != OTA_OK) {
        LOGE(TAG, "ota_write failed: %s", ota_err_to_str(err));
        reply_error(c, 500, ota_err_to_str(err));
        s_active = false;
        return (size_t)-1;
    }
    s_received += take;

    if (s_received % (256 * 1024) < 16384 || s_received == s_expected) {
        LOGI(TAG, "Progress: %u/%u bytes", (unsigned)s_received,
             (unsigned)s_expected);
    }

    if (s_received >= s_expected) {
        LOGI(TAG, "Upload complete, verifying...");
        ota_err_t res = ota_end();
        if (res != OTA_OK) {
            LOGE(TAG, "Verification FAILED: %s", ota_err_to_str(res));
            reply_error(c, 500, ota_err_to_str(res));
            s_active = false;
            return (size_t)-1;
        }

        ota_status_t st;
        char sha_hex[65] = "";
        if (ota_get_status(&st) == OTA_OK) {
            sha_to_hex(st.sha256, sha_hex);
        }
        LOGI(TAG, "Verified OK (sha256=%s), rebooting", sha_hex);
        reply_ok_and_reboot(c, sha_hex);
        s_active = false;
        ota_reboot();   /* 500ms 后重启，响应可先行发出 */
    }

    return take;
}

// ---------- STREAM: on_close ----------
static void ota_upload_on_close(struct mg_connection *c) {
    (void)c;
    if (s_active) {
        LOGW(TAG, "Client aborted upload at %u bytes", (unsigned)s_received);
        ota_abort();
        s_active = false;
    }
}

// ---------- 精确路由：状态查询 ----------
static void ota_status_handler(struct mg_connection *c,
                               struct mg_http_message *hm) {
    (void)hm;
    ota_status_t st;
    if (ota_get_status(&st) != OTA_OK) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"ota not ready\"}");
        return;
    }

    char sha_hex[65] = "";
    if (st.state == OTA_STATE_DONE) {
        sha_to_hex(st.sha256, sha_hex);
    }

    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                  "{\"state\":\"%s\",\"received\":%u,\"expected\":%u,"
                  "\"current_slot\":%d,\"target_slot\":%d,"
                  "\"pending_verify\":%s,"
                  "\"version\":\"%s\",\"project\":\"%s\",\"sha256\":\"%s\"}",
                  ota_state_to_str(st.state),
                  (unsigned)st.received, (unsigned)st.expected,
                  st.current_slot, st.target_slot,
                  st.pending_verify ? "true" : "false",
                  st.version, st.project, sha_hex);
}

int web_ota_routes_init(void) {
    web_stream_route_t up;
    up.method = "POST";
    up.uri = "/api/ota/firmware";
    up.on_hdrs = ota_upload_on_hdrs;
    up.on_data = ota_upload_on_data;
    up.on_close = ota_upload_on_close;
    int rc = web_register_stream_route(&up);

    up.uri = "/ota/update";   /* 旧脚本路径兼容 */
    int rc2 = web_register_stream_route(&up);

    web_route_t st1 = {"GET", "/api/ota/status", ota_status_handler};
    web_route_t st2 = {"GET", "/ota/progress", ota_status_handler};
    web_register_route(&st1);
    web_register_route(&st2);

    return (rc == 0 && rc2 == 0) ? 0 : -1;
}
