/**
 * @file web_ota.c
 * @brief Web 模块内置的 OTA 端点
 *
 * 端点：
 *   POST /api/ota/firmware   流式固件上传（推荐）
 *   POST /ota/update         同上（旧脚本路径兼容）
 *   POST /api/dtb/firmware   独立设备树更新（写非活动槽并翻转 active）
 *   GET  /api/ota/status     升级状态（进度/SHA256/槽位/版本）
 *   GET  /ota/progress       同上（旧脚本路径兼容）
 *
 * 固件上传两种格式（流式，内存占用恒定）：
 *   - OVSO 容器：[96B 头部 | app.bin | dtb.bin?]，app 写 OTA 槽、
 *     dtb 写非活动设备树槽，双校验通过后 set_boot + 登记 trial 槽
 *     （app 15s 确认时正式翻转，见 dtb_ab.h）；
 *   - 无 magic 的纯 app 流（旧脚本兼容）：整流写 OTA 槽。
 * 容器格式与 scripts/ovs_pack_payload.py 保持一致，改动需两端同步。
 */

#include "web_ota.h"
#include "mem.h"
#include "web.h"
#include "ota.h"
#include "dtb_ab.h"
#include "mongoose.h"
#include "logger.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "[WEB][OTA]";

/* 分区表 ota_0/ota_1 各 0x280000 (2.5MB)；上传总量再放宽头部与 dtb */
#define OTA_UPLOAD_MAX_SIZE  (0x280000u + 96u + (32 * 1024u))
#define OTA_PAYLOAD_HDR_SIZE (96u)
#define OTA_MAX_DTB_SIZE     (32 * 1024 + 64)   /* 槽容器上限（含余量） */

/* OVSO 容器常量（与 ovs_pack_payload.py 一致） */
#define PAYLOAD_MAGIC   0x4F53564FU      /* "OVSO" 小端 */
#define PAYLOAD_VERSION 1
#define PAYLOAD_FLAG_DTB 0x01

/* 单上传会话状态（web 任务单线程访问） */
static bool s_active = false;
static size_t s_expected = 0;       /* app 部分大小 */
static size_t s_received = 0;

/* 容器头部解析与 dtb 接收 */
static uint8_t s_hdr_buf[OTA_PAYLOAD_HDR_SIZE];
static size_t s_hdr_got = 0;
static bool s_hdr_done = false;     /* 头部已解析（或确认旧式无头部） */
static bool s_has_dtb = false;
static bool s_dtb_stage = false;    /* app 收完，正在收 dtb */
static uint8_t *s_dtb_buf = NULL;
static size_t s_dtb_size = 0;
static size_t s_dtb_got = 0;

/* 独立设备树上传会话 */
static uint8_t *s_dtbup_buf = NULL;
static size_t s_dtbup_size = 0;
static size_t s_dtbup_got = 0;

static void reply_error(struct mg_connection *c, int code, const char *msg) {
    mg_printf(c,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/json\r\n"
              "Connection: close\r\n"
              "Content-Length: %d\r\n"
              "\r\n"
              "{\"status\":\"error\",\"error\":\"%s\"}",
              code, code == 413 ? "Payload Too Large" :
                    code == 409 ? "Conflict" :
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

// ---------- 固件上传会话生命周期 ----------
static void fw_session_reset(void) {
    s_active = false;
    s_expected = 0;
    s_received = 0;
    s_hdr_got = 0;
    s_hdr_done = false;
    s_has_dtb = false;
    s_dtb_stage = false;
    mem_free(s_dtb_buf);
    s_dtb_buf = NULL;
    s_dtb_size = 0;
    s_dtb_got = 0;
}

/* app 部分收完且无 dtb：校验并提交 */
static size_t fw_finish_plain(struct mg_connection *c) {
    LOGI(TAG, "Upload complete, verifying...");
    ota_err_t res = ota_end();
    if (res != OTA_OK) {
        LOGE(TAG, "Verification FAILED: %s", ota_err_to_str(res));
        reply_error(c, 500, ota_err_to_str(res));
        fw_session_reset();
        return (size_t)-1;
    }

    ota_status_t st;
    char sha_hex[65] = "";
    if (ota_get_status(&st) == OTA_OK) {
        sha_to_hex(st.sha256, sha_hex);
    }
    LOGI(TAG, "Verified OK (sha256=%s), rebooting", sha_hex);
    reply_ok_and_reboot(c, sha_hex);
    fw_session_reset();
    ota_reboot();   /* esp_timer 延迟重启，响应可先行发出 */
    return 0;
}

/* app + dtb 全部收完：写树槽 → 校验 app → set_boot → 登记 trial */
static size_t fw_finish_with_dtb(struct mg_connection *c) {
    int slot = -1;
    if (dtb_ab_write_inactive(s_dtb_buf, s_dtb_size, &slot) != 0 || slot < 0) {
        LOGE(TAG, "dtb write failed");
        reply_error(c, 500, "dtb write failed");
        fw_session_reset();
        return (size_t)-1;
    }

    LOGI(TAG, "Upload complete (app+dtb), verifying app...");
    ota_err_t res = ota_end();          /* 校验 app 镜像并 set_boot_partition */
    if (res != OTA_OK) {
        LOGE(TAG, "Verification FAILED: %s", ota_err_to_str(res));
        reply_error(c, 500, ota_err_to_str(res));
        fw_session_reset();
        return (size_t)-1;
    }

    if (dtb_ab_commit_trial(slot) != 0) {
        /* 极少见：NVS 写失败。app 将以旧树运行新固件，可重推修复 */
        LOGE(TAG, "dtb trial commit failed (slot %d)", slot);
        reply_error(c, 500, "dtb commit failed");
        fw_session_reset();
        return (size_t)-1;
    }

    ota_status_t st;
    char sha_hex[65] = "";
    if (ota_get_status(&st) == OTA_OK) {
        sha_to_hex(st.sha256, sha_hex);
    }
    LOGI(TAG, "Verified OK (sha256=%s, dtb slot %d), rebooting",
         sha_hex, slot);
    reply_ok_and_reboot(c, sha_hex);
    fw_session_reset();
    ota_reboot();
    return 0;
}

/* 解析 96B 容器头部；返回 0=OVSO 容器，1=旧式纯 app 流，-1=非法 */
static int fw_parse_hdr(struct mg_connection *c) {
    uint32_t magic, app_size, dtb_size;
    memcpy(&magic, s_hdr_buf, 4);
    if (magic != PAYLOAD_MAGIC) {
        return 1;   /* 旧式纯 app 流：已缓冲字节全是 app 数据 */
    }
    uint8_t version = s_hdr_buf[4];
    uint8_t flags = s_hdr_buf[5];
    memcpy(&app_size, s_hdr_buf + 8, 4);
    memcpy(&dtb_size, s_hdr_buf + 12, 4);

    if (version != PAYLOAD_VERSION) {
        reply_error(c, 400, "unsupported payload version");
        return -1;
    }
    if (app_size == 0 || app_size > OTA_UPLOAD_MAX_SIZE) {
        reply_error(c, 400, "bad app size");
        return -1;
    }
    if ((flags & PAYLOAD_FLAG_DTB) && dtb_size > OTA_MAX_DTB_SIZE) {
        reply_error(c, 413, "dtb exceeds slot size");
        return -1;
    }
    if ((flags & PAYLOAD_FLAG_DTB) == 0) {
        dtb_size = 0;
    }
    /* 上传总量 = 头部 + app + dtb，必须与 Content-Length 一致 */
    if (OTA_PAYLOAD_HDR_SIZE + app_size + dtb_size != s_expected) {
        reply_error(c, 400, "sizes inconsistent");
        return -1;
    }

    ota_err_t err = ota_begin(app_size);
    if (err != OTA_OK) {
        reply_error(c, 503, ota_err_to_str(err));
        return -1;
    }

    s_expected = app_size;
    s_has_dtb = (dtb_size > 0);
    if (s_has_dtb) {
        s_dtb_buf = mem_malloc(dtb_size);
        if (!s_dtb_buf) {
            reply_error(c, 500, "oom for dtb");
            return -1;
        }
        s_dtb_size = dtb_size;
        s_dtb_got = 0;
        LOGI(TAG, "OVSO payload: app %uB + dtb %uB",
             (unsigned)app_size, (unsigned)dtb_size);
    }
    return 0;
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

    /* ota_begin 延后到头部解析后（需要区分容器/旧式流确定 app 大小） */
    s_active = true;
    s_expected = (size_t)total;
    s_received = 0;
    LOGI(TAG, "Upload started: %lld bytes", total);
    return 0;   /* 接管连接 */
}

// ---------- STREAM: on_data ----------
static size_t ota_upload_on_data(struct mg_connection *c,
                                 const char *data, size_t len) {
    size_t pos = 0;

    /* 1. 头部阶段：攒满 96B 才能区分容器/旧式流 */
    if (!s_hdr_done) {
        size_t want = OTA_PAYLOAD_HDR_SIZE - s_hdr_got;
        size_t take = (len - pos) < want ? (len - pos) : want;
        memcpy(s_hdr_buf + s_hdr_got, data + pos, take);
        s_hdr_got += take;
        pos += take;
        if (s_hdr_got < OTA_PAYLOAD_HDR_SIZE) {
            return pos;
        }
        int rc = fw_parse_hdr(c);
        if (rc < 0) {
            fw_session_reset();
            return (size_t)-1;
        }
        s_hdr_done = true;
        if (rc == 1) {
            /* 旧式流：缓冲区里的"头部字节"其实是 app 数据开头 */
            size_t n = s_expected - s_received;
            if (s_hdr_got > n) s_hdr_got = n;
            ota_err_t err = ota_write(s_hdr_buf, s_hdr_got);
            if (err != OTA_OK) {
                LOGE(TAG, "ota_write failed: %s", ota_err_to_str(err));
                reply_error(c, 500, ota_err_to_str(err));
                fw_session_reset();
                return (size_t)-1;
            }
            s_received += s_hdr_got;
        }
    }

    /* 2. app 段 */
    if (!s_dtb_stage && s_received < s_expected) {
        size_t remaining = s_expected - s_received;
        size_t take = (len - pos) < remaining ? (len - pos) : remaining;
        if (take > 0) {
            ota_err_t err = ota_write(data + pos, take);
            if (err != OTA_OK) {
                LOGE(TAG, "ota_write failed: %s", ota_err_to_str(err));
                reply_error(c, 500, ota_err_to_str(err));
                fw_session_reset();
                return (size_t)-1;
            }
            s_received += take;
            pos += take;
            if (s_received % (256 * 1024) < 16384) {
                LOGI(TAG, "Progress: %u/%u bytes", (unsigned)s_received,
                     (unsigned)s_expected);
            }
        }
    }

    /* 3. app 收完：分流 */
    if (!s_dtb_stage && s_received >= s_expected) {
        if (!s_has_dtb) {
            size_t rc = fw_finish_plain(c);
            if (rc == (size_t)-1) return rc;
            return pos;
        }
        s_dtb_stage = true;
        LOGI(TAG, "app done (%uB), receiving dtb (%uB)",
             (unsigned)s_received, (unsigned)s_dtb_size);
    }

    /* 4. dtb 段 */
    if (s_dtb_stage) {
        size_t remaining = s_dtb_size - s_dtb_got;
        size_t take = (len - pos) < remaining ? (len - pos) : remaining;
        if (take > 0) {
            memcpy(s_dtb_buf + s_dtb_got, data + pos, take);
            s_dtb_got += take;
            pos += take;
        }
        if (s_dtb_got >= s_dtb_size) {
            size_t rc = fw_finish_with_dtb(c);
            if (rc == (size_t)-1) return rc;
            return pos;
        }
    }

    return pos;
}

// ---------- STREAM: on_close ----------
static void ota_upload_on_close(struct mg_connection *c) {
    (void)c;
    if (s_active) {
        LOGW(TAG, "Client aborted upload at %u bytes", (unsigned)s_received);
        ota_abort();
        fw_session_reset();
    }
}

// ---------- 独立设备树上传（/api/dtb/firmware） ----------
static int dtb_upload_on_hdrs(struct mg_connection *c,
                              struct mg_http_message *hm) {
    if (s_dtbup_buf) {
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
    if (total <= 0 || total > OTA_MAX_DTB_SIZE) {
        reply_error(c, 413, "dtb exceeds slot size");
        return -1;
    }
    s_dtbup_buf = mem_malloc((size_t)total);
    if (!s_dtbup_buf) {
        reply_error(c, 500, "oom");
        return -1;
    }
    s_dtbup_size = (size_t)total;
    s_dtbup_got = 0;
    LOGI(TAG, "[dtb] upload started: %lld bytes", total);
    return 0;
}

static size_t dtb_upload_on_data(struct mg_connection *c,
                                 const char *data, size_t len) {
    size_t remaining = s_dtbup_size - s_dtbup_got;
    size_t take = len < remaining ? len : remaining;
    memcpy(s_dtbup_buf + s_dtbup_got, data, take);
    s_dtbup_got += take;

    if (s_dtbup_got >= s_dtbup_size) {
        int slot = -1;
        if (dtb_ab_write_inactive(s_dtbup_buf, s_dtbup_size, &slot) != 0) {
            LOGE(TAG, "[dtb] write failed");
            reply_error(c, 500, "dtb write failed");
            mem_free(s_dtbup_buf);
            s_dtbup_buf = NULL;
            return (size_t)-1;
        }
        if (dtb_ab_set_active(slot) != 0) {
            reply_error(c, 500, "dtb activate failed");
            mem_free(s_dtbup_buf);
            s_dtbup_buf = NULL;
            return (size_t)-1;
        }
        char body[96];
        int blen = snprintf(body, sizeof(body),
                            "{\"status\":\"ok\",\"slot\":%d,"
                            "\"message\":\"takes effect after reboot\"}",
                            slot);
        mg_printf(c,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: application/json\r\n"
                  "Connection: close\r\n"
                  "Content-Length: %d\r\n"
                  "\r\n"
                  "%.*s", blen, blen, body);
        c->is_draining = 1;
        LOGI(TAG, "[dtb] activated slot %d", slot);
        mem_free(s_dtbup_buf);
        s_dtbup_buf = NULL;
    }
    return take;
}

static void dtb_upload_on_close(struct mg_connection *c) {
    (void)c;
    if (s_dtbup_buf) {
        LOGW(TAG, "[dtb] upload aborted at %u bytes", (unsigned)s_dtbup_got);
        mem_free(s_dtbup_buf);
        s_dtbup_buf = NULL;
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
                  "\"pending_verify\":%s,\"dtb_slot\":%d,"
                  "\"version\":\"%s\",\"project\":\"%s\",\"sha256\":\"%s\"}",
                  ota_state_to_str(st.state),
                  (unsigned)st.received, (unsigned)st.expected,
                  st.current_slot, st.target_slot,
                  st.pending_verify ? "true" : "false",
                  dtb_ab_active_slot(),
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

    web_stream_route_t dtb;
    dtb.method = "POST";
    dtb.uri = "/api/dtb/firmware";
    dtb.on_hdrs = dtb_upload_on_hdrs;
    dtb.on_data = dtb_upload_on_data;
    dtb.on_close = dtb_upload_on_close;
    int rc3 = web_register_stream_route(&dtb);

    web_route_t st1 = {"GET", "/api/ota/status", ota_status_handler};
    web_route_t st2 = {"GET", "/ota/progress", ota_status_handler};
    web_register_route(&st1);
    web_register_route(&st2);

    return (rc == 0 && rc2 == 0 && rc3 == 0) ? 0 : -1;
}
