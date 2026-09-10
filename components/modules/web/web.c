/**
 * @file web.c
 * @brief Web 服务核心实现
 */

#include "web.h"
#include "mem.h"
#include "web_data.h"       /* 由 tools/fs_to_c.py 生成的内嵌资源 */
#include "mongoose.h"
#include "net_mgr.h"
#include "wifi.h"
#include "heartbeat.h"
#include "tasker.h"
#include "event_bus.h"
#include "holder.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "esp_spiffs.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_system.h"

// web 模块内置的 OTA 端点（流式上传 + 状态查询）
int web_ota_routes_init(void);
// [WEB] 只读信息路由（/api/sensor、/api/net/info、/api/time、/i18n/*，契约 I5）
int web_api_sysinfo_init(void);

static const char *TAG = "[WEB]";

#define WEB_TASK_STACK    (8192)
#define WEB_TASK_PRIO     (5)

// ---------- 服务状态 ----------
static struct mg_mgr s_mgr;
static int s_server_running = 0;
static TaskHandle_t s_web_task = NULL;

// ---------- 路由注册表 ----------
static web_route_t s_routes[WEB_MAX_ROUTES];
static int s_route_count = 0;
static web_stream_route_t s_stream_routes[WEB_MAX_ROUTES];
static int s_stream_count = 0;

// ---------- 流式连接上下文（挂到 c->fn_data） ----------
typedef struct {
    const web_stream_route_t *route;
} web_stream_ctx_t;

static void stream_feed(web_stream_ctx_t *ctx, struct mg_connection *c);

// ---------- WebSocket 订阅者 ----------
struct ws_client {
    struct mg_connection *c;
    struct ws_client *next;
};
static struct ws_client *s_ws_clients = NULL;

// WS 推送桥接队列：其他任务 → web 任务执行 mg_ws_send（线程安全）
typedef struct {
    char *json;
    size_t len;
} ws_msg_t;
static QueueHandle_t s_ws_queue = NULL;
#define WS_QUEUE_DEPTH 8

int web_register_route(const web_route_t *route) {
    if (!route || !route->method || !route->uri || !route->fn) return -1;
    for (int i = 0; i < s_route_count; i++) {
        if (strcmp(s_routes[i].method, route->method) == 0 &&
            strcmp(s_routes[i].uri, route->uri) == 0) return -1;
    }
    if (s_route_count >= WEB_MAX_ROUTES) return -2;
    s_routes[s_route_count++] = *route;
    return 0;
}

int web_register_stream_route(const web_stream_route_t *route) {
    if (!route || !route->method || !route->uri ||
        !route->on_hdrs || !route->on_data) return -1;
    for (int i = 0; i < s_stream_count; i++) {
        if (strcmp(s_stream_routes[i].uri, route->uri) == 0) return -1;
    }
    if (s_stream_count >= WEB_MAX_ROUTES) return -2;
    s_stream_routes[s_stream_count++] = *route;
    return 0;
}

// ===========================================================================
// SPIFFS 初始化 & Web 资源部署
// ===========================================================================

static int ensure_dir(const char *path) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') tmp[len - 1] = 0;

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
    return 0;
}

static int write_data_to_file(const char *filepath,
                              const unsigned char *data, size_t size) {
    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        LOGE(TAG, "fopen failed: %s", filepath);
        return -1;
    }
    if (size > 0 && fwrite(data, 1, size, fp) != size) {
        LOGE(TAG, "fwrite failed for %s", filepath);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

int web_spiffs_init(void) {
    // 兼容 main 已挂载 SPIFFS 的情况：已挂载则跳过注册
    bool mounted = esp_spiffs_mounted("spiffs");
    if (!mounted) {
        esp_vfs_spiffs_conf_t conf = {
            .base_path = WEB_SPIFFS_MOUNT,
            .partition_label = "spiffs",
            .max_files = 10,
            .format_if_mount_failed = true,
        };
        esp_err_t ret = esp_vfs_spiffs_register(&conf);
        if (ret != ESP_OK) {
            LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
            return -1;
        }
    }

    // 读取已部署资源的版本哈希
    char spiffs_hash[65] = {0};
    FILE *hf = fopen(WEB_SPIFFS_MOUNT "/.web_hash", "r");
    bool hash_match = false;
    if (hf) {
        size_t nread = fread(spiffs_hash, 1, 64, hf);
        spiffs_hash[nread] = '\0';
        size_t slen = strlen(spiffs_hash);
        while (slen > 0 && (spiffs_hash[slen-1] == '\n' || spiffs_hash[slen-1] == '\r')) {
            spiffs_hash[--slen] = '\0';
        }
        fclose(hf);
        hash_match = (strcmp(spiffs_hash, WEB_DATA_HASH) == 0);
    }

    if (hash_match) {
        LOGI(TAG, "SPIFFS web resources up-to-date (hash=%s)", spiffs_hash);
        return 0;
    }

    LOGI(TAG, "Deploying web resources (hash mismatch or first boot)");

    // 重新部署：格式化 + 全量写入
    LOGI(TAG, "Formatting SPIFFS...");
    esp_vfs_spiffs_unregister("spiffs");
    esp_spiffs_format("spiffs");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = WEB_SPIFFS_MOUNT,
        .partition_label = "spiffs",
        .max_files = 10,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        LOGE(TAG, "SPIFFS remount failed: %s", esp_err_to_name(ret));
        return -1;
    }

    for (int i = 0; i < web_files_count; i++) {
        const web_file_t *f = &web_files[i];

        char fullpath[256];
        snprintf(fullpath, sizeof(fullpath), "%s%s", WEB_SPIFFS_MOUNT, f->path);

        char dirpath[256];
        strncpy(dirpath, fullpath, sizeof(dirpath) - 1);
        dirpath[sizeof(dirpath) - 1] = '\0';
        char *last_slash = strrchr(dirpath, '/');
        if (last_slash) {
            *last_slash = '\0';
            ensure_dir(dirpath);
        }

        if (write_data_to_file(fullpath, f->data, f->size) != 0) {
            LOGE(TAG, "Failed to write: %s", f->path);
            return -1;
        }
    }

    hf = fopen(WEB_SPIFFS_MOUNT "/.web_hash", "w");
    if (hf) {
        fprintf(hf, "%s\n", WEB_DATA_HASH);
        fclose(hf);
    } else {
        LOGE(TAG, "Failed to write .web_hash");
    }

    LOGI(TAG, "All web resources written to SPIFFS");
    return 0;
}

// ===========================================================================
// 静态文件
// ===========================================================================

static const char *get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    if (strcasecmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcasecmp(ext, ".css") == 0)  return "text/css; charset=utf-8";
    if (strcasecmp(ext, ".js") == 0)   return "application/javascript; charset=utf-8";
    if (strcasecmp(ext, ".json") == 0) return "application/json";
    if (strcasecmp(ext, ".png") == 0)  return "image/png";
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, ".gif") == 0)  return "image/gif";
    if (strcasecmp(ext, ".svg") == 0)  return "image/svg+xml";
    if (strcasecmp(ext, ".ico") == 0)  return "image/x-icon";
    if (strcasecmp(ext, ".woff") == 0)  return "font/woff";
    if (strcasecmp(ext, ".woff2") == 0) return "font/woff2";
    if (strcasecmp(ext, ".ttf") == 0)   return "font/ttf";

    return "application/octet-stream";
}

static void serve_static_file(struct mg_connection *c, const char *uri) {
    const char *served_path = uri;
    if (strcmp(uri, "/") == 0) served_path = "/index.html";

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s%s", WEB_SPIFFS_MOUNT, served_path);

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        mg_http_reply(c, 404, "Content-Type: application/json\r\n",
                      "{\"error\":\"not found\"}");
        return;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    const char *mime = get_mime_type(served_path);

    mg_printf(c,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: %s\r\n"
              "Content-Length: %ld\r\n"
              "Connection: close\r\n"
              "\r\n",
              mime, fsize);

    char buf[512];
    size_t nread;
    while ((nread = fread(buf, 1, sizeof(buf), fp)) > 0) {
        mg_send(c, buf, nread);
    }
    fclose(fp);
    c->is_draining = 1;
}

// ===========================================================================
// 内置 REST API（经注册表注册）
// ===========================================================================

// GET /api/wifi/scan
static void handle_wifi_scan(struct mg_connection *c, struct mg_http_message *hm) {
    (void)hm;
    wifi_ap_info_t aps[20];
    int count = wifi_scan_aps(aps, 20);

    if (count <= 0) {
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "{\"aps\":[],\"count\":0}");
        return;
    }

    int est_len = 64 * count + 64;
    char *buf = mem_malloc(est_len);
    if (!buf) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"oom\"}");
        return;
    }

    int off = snprintf(buf, est_len, "{\"aps\":[");
    for (int i = 0; i < count; i++) {
        if (i > 0) off += snprintf(buf + off, est_len - off, ",");
        char escaped[70];
        int e = 0;
        for (int j = 0; j < 33 && aps[i].ssid[j]; j++) {
            if (aps[i].ssid[j] == '"' || aps[i].ssid[j] == '\\') {
                escaped[e++] = '\\';
            }
            escaped[e++] = aps[i].ssid[j];
        }
        escaped[e] = '\0';
        off += snprintf(buf + off, est_len - off,
                        "{\"ssid\":\"%s\",\"rssi\":%d,\"authmode\":%d}",
                        escaped, aps[i].rssi, aps[i].authmode);
    }
    off += snprintf(buf + off, est_len - off, "],\"count\":%d}", count);

    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                  "%.*s", off, buf);
    mem_free(buf);
}

// GET /api/wifi/status — net_mgr 状态（含热切换进度）
static void handle_wifi_status(struct mg_connection *c, struct mg_http_message *hm) {
    (void)hm;
    net_status_t st = {0};
    net_mgr_get_status(&st);

    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                  "{\"mode\":\"%s\",\"state\":\"%s\","
                  "\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,"
                  "\"switching\":%s,\"switch_elapsed_sec\":%d,"
                  "\"sta_configured\":%s}",
                  net_mode_to_str(st.mode),
                  net_state_to_str(st.state),
                  st.ssid, st.ip, st.rssi,
                  st.switching ? "true" : "false",
                  st.switch_elapsed_sec,
                  st.sta_configured ? "true" : "false");
}

// POST /api/wifi/connect — 经配网通道统一入口
static void handle_wifi_connect(struct mg_connection *c, struct mg_http_message *hm) {
    // 简单 JSON 提取："key":"value"（配网场景值不含转义字符）
    struct mg_str body = hm->body;
    char ssid[33] = {0}, password[65] = {0};

    char pattern[24];
    const char *start, *end, *pos;

    snprintf(pattern, sizeof(pattern), "\"ssid\":\"");
    start = NULL;
    for (pos = body.buf; body.len >= (size_t)(pos - body.buf) + 8; pos++) {
        if (strncmp(pos, pattern, 8) == 0) { start = pos + 8; break; }
    }
    if (start) {
        end = memchr(start, '"', body.buf + body.len - start);
        if (end && end - start > 0 && end - start <= 32) {
            memcpy(ssid, start, end - start);
        }
    }
    snprintf(pattern, sizeof(pattern), "\"password\":\"");
    start = NULL;
    for (pos = body.buf; body.len >= (size_t)(pos - body.buf) + 12; pos++) {
        if (strncmp(pos, pattern, 12) == 0) { start = pos + 12; break; }
    }
    if (start) {
        end = memchr(start, '"', body.buf + body.len - start);
        if (end && end - start >= 0 && end - start <= 64) {
            memcpy(password, start, end - start);
        }
    }

    if (ssid[0] == '\0') {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"invalid ssid\"}");
        return;
    }

    LOGI(TAG, "WiFi connect request (web provisioning): SSID=%s", ssid);

    net_err_t err = net_provision_submit(ssid, password);
    if (err != NET_OK) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"%s\"}", net_err_to_str(err));
        return;
    }

    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                  "{\"status\":\"ok\",\"message\":\"switching\"}");
}

// GET /api/status — 设备总览
static void handle_device_status(struct mg_connection *c, struct mg_http_message *hm) {
    (void)hm;
    net_status_t net = {0};
    net_mgr_get_status(&net);

    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                  "{\"device\":\"ESP32-S3\",\"uptime_sec\":%llu,"
                  "\"rssi\":%d,\"free_heap\":%u,"
                  "\"net_mode\":\"%s\",\"net_state\":\"%s\","
                  "\"ip\":\"%s\"}",
                  (unsigned long long)heartbeat_get_uptime_sec(),
                  heartbeat_get_rssi(),
                  (unsigned)esp_get_free_heap_size(),
                  net_mode_to_str(net.mode),
                  net_state_to_str(net.state),
                  net.ip);
}

// GET /api/hello — 连通性测试
static void handle_hello(struct mg_connection *c, struct mg_http_message *hm) {
    (void)hm;
    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                  "{\"message\":\"Hello from ESP32!\",\"status\":\"ok\"}");
}

// ---------------------------------------------------------------------------
// POST /api/web/update — 网页包更新（tar 到 SPIFFS，无需整机 OTA）
// 网页包体积小（<1MB），走精确路由整包缓冲即可
// ---------------------------------------------------------------------------

/**
 * @brief 最小 tar 解析器：从 tar 数据中提取文件到 SPIFFS
 *
 * tar 格式（USTAR）：512B 头 + 数据(补齐512B) + ... + 两个 512B 全零块
 * @return 提取的文件数量，<0 表示失败
 */
static int tar_extract_to_spiffs(const unsigned char *data, size_t len) {
    size_t offset = 0;
    int files_extracted = 0;
    int consecutive_zero_blocks = 0;

    while (offset + 512 <= len) {
        const unsigned char *header = data + offset;

        int is_zero = 1;
        for (int i = 0; i < 512; i++) {
            if (header[i] != 0) { is_zero = 0; break; }
        }
        if (is_zero) {
            if (++consecutive_zero_blocks >= 2) break;
            offset += 512;
            continue;
        }
        consecutive_zero_blocks = 0;

        char filename[256];
        memcpy(filename, header, 100);
        filename[100] = '\0';
        int fnlen = (int)strlen(filename);
        while (fnlen > 0 && filename[fnlen-1] == ' ') filename[--fnlen] = '\0';
        if (fnlen == 0) { offset += 512; continue; }

        // 目录条目（以 / 结尾）
        if (filename[fnlen-1] == '/') {
            char dirpath[256];
            snprintf(dirpath, sizeof(dirpath), "%s%.*s",
                     WEB_SPIFFS_MOUNT, fnlen, filename);
            size_t dplen = strlen(dirpath);
            if (dplen > 0 && dirpath[dplen-1] == '/') dirpath[dplen-1] = '\0';
            ensure_dir(dirpath);
            offset += 512;
            continue;
        }

        // 文件大小（124-135，八进制 ASCII）
        char size_str[13];
        memcpy(size_str, header + 124, 12);
        size_str[12] = '\0';
        size_t file_size = (size_t)strtoul(size_str, NULL, 8);

        offset += 512;
        if (offset + file_size > len) {
            LOGE(TAG, "Tar: file %s exceeds data boundary", filename);
            return -1;
        }

        const char *clean_name = filename;
        while (*clean_name == '/') clean_name++;
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", WEB_SPIFFS_MOUNT,
                 clean_name);

        char parent[512];
        snprintf(parent, sizeof(parent), "%s", filepath);
        char *last_slash = strrchr(parent, '/');
        if (last_slash) {
            *last_slash = '\0';
            mkdir(parent, 0755);
        }

        FILE *fp = fopen(filepath, "wb");
        if (!fp) {
            LOGE(TAG, "Tar: failed to create %s", filepath);
            return -1;
        }
        size_t written = fwrite(data + offset, 1, file_size, fp);
        fclose(fp);
        if (written != file_size) {
            LOGE(TAG, "Tar: write mismatch for %s (%zu/%zu)",
                 filename, written, file_size);
            return -1;
        }

        files_extracted++;
        LOGI(TAG, "  Extracted: %s (%zu bytes)", clean_name, file_size);

        offset += (file_size + 511) & ~511u;
    }

    return files_extracted;
}

static void handle_web_update(struct mg_connection *c, struct mg_http_message *hm) {
    size_t body_len = hm->body.len;
    if (body_len == 0) {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"empty body\"}");
        return;
    }

    LOGI(TAG, "Web update: received %zu bytes", body_len);

    // 清空旧网页文件（仅顶层文件；子目录随 format 重建）
    DIR *dir = opendir(WEB_SPIFFS_MOUNT);
    if (dir) {
        struct dirent *entry;
        char fullpath[320];
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0) continue;
            snprintf(fullpath, sizeof(fullpath), "%s/%s",
                     WEB_SPIFFS_MOUNT, entry->d_name);
            struct stat st;
            if (stat(fullpath, &st) == 0 && !S_ISDIR(st.st_mode)) {
                unlink(fullpath);
            }
        }
        closedir(dir);
    }

    int count = tar_extract_to_spiffs((const unsigned char *)hm->body.buf,
                                      body_len);
    if (count < 0) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"tar extraction failed\"}");
        return;
    }

    LOGI(TAG, "Web update complete: %d files extracted", count);
    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                  "{\"status\":\"ok\",\"files\":%d,"
                  "\"message\":\"web updated, please refresh\"}", count);
}

// POST /api/net/mode — 网络模式热切换（{"mode":"sta"|"ap"|"off"}，免重启）
static bool body_contains(struct mg_http_message *hm,
                          const char *needle, size_t nlen) {
    const char *p = hm->body.buf;
    size_t n = hm->body.len;
    if (n < nlen) return false;
    for (size_t i = 0; i + nlen <= n; i++) {
        if (memcmp(p + i, needle, nlen) == 0) return true;
    }
    return false;
}

static void handle_net_mode(struct mg_connection *c, struct mg_http_message *hm) {
    net_mode_t target;
    if (body_contains(hm, "\"ap\"", 4)) {
        target = NET_MODE_AP;
    } else if (body_contains(hm, "\"sta\"", 5)) {
        target = NET_MODE_STA;
    } else if (body_contains(hm, "\"off\"", 5)) {
        target = NET_MODE_OFF;
    } else {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"need \\\"mode\\\": \\\"sta\\\"|\\\"ap\\\"|\\\"off\\\"\"}");
        return;
    }

    net_mode_t prev = NET_MODE_OFF;
    net_err_t err = net_mgr_switch_mode(target, &prev);
    if (err != NET_OK) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"%s\"}", net_err_to_str(err));
        return;
    }

    net_status_t st = {0};
    net_mgr_get_status(&st);
    LOGI(TAG, "Net mode switch request: %s -> %s",
         net_mode_to_str(prev), net_mode_to_str(target));
    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                  "{\"status\":\"ok\",\"previous\":\"%s\",\"mode\":\"%s\","
                  "\"state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\"}",
                  net_mode_to_str(prev), net_mode_to_str(st.mode),
                  net_state_to_str(st.state), st.ssid, st.ip);
}

// GET /api/modules — holder 全模块状态（Phase 1 验收：init 耗时/状态/错误）
static void handle_modules(struct mg_connection *c, struct mg_http_message *hm) {
    (void)hm;
    /* mg_http_reply 一次调用即完整响应，须先拼缓冲再一次发送 */
    char body[2048];
    size_t off = 0;
    off += (size_t)snprintf(body + off, sizeof(body) - off, "{\"modules\":[");
    uint32_t count = holder_get_module_count();
    for (uint32_t i = 0; i < count && off < sizeof(body) - 160; i++) {
        const char* name = holder_get_module_name((int)i);
        holder_module_info_t info;
        if (name == NULL || holder_get_module_info(name, &info) != HOLDER_OK) continue;
        const char* state = "unknown";
        switch (info.state) {
            case HOLDER_MODULE_STATE_REGISTERED:    state = "registered";    break;
            case HOLDER_MODULE_STATE_INITIALIZING:  state = "initializing";  break;
            case HOLDER_MODULE_STATE_READY:         state = "ready";         break;
            case HOLDER_MODULE_STATE_ERROR:         state = "error";         break;
            case HOLDER_MODULE_STATE_DISABLED:      state = "disabled";      break;
            default:                                state = "unknown";       break;
        }
        off += (size_t)snprintf(body + off, sizeof(body) - off,
                      "%s{\"name\":\"%s\",\"state\":\"%s\",\"required\":%s,"
                      "\"init_time_ms\":%lu,\"error\":\"%s\"}",
                      i ? "," : "", name, state,
                      info.required ? "true" : "false",
                      (unsigned long)info.init_time_ms,
                      info.last_error ? info.last_error : "");
    }
    off += (size_t)snprintf(body + off, sizeof(body) - off, "]}");
    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%.*s",
                  (int)off, body);
}

// ---------------------------------------------------------------------------
// 事件总线订阅 → WS 广播（Phase 1 验收: 真实订阅者 ≥6；
// 回调运行于事件任务上下文，web_ws_broadcast 内部队列桥接线程安全）
// ---------------------------------------------------------------------------
static int on_event_ws_push(const event_t* e, void* ud) {
    (void)ud;
    char buf[192];
    const char* name = event_bus_get_type_name(e->header.type);

    if (e->header.type == EVENT_SENSOR_TEMP_HUMIDITY &&
        e->header.data_len >= sizeof(event_sensor_temp_humidity_t)) {
        event_sensor_temp_humidity_t d;
        memcpy(&d, e->data, sizeof(d));
        snprintf(buf, sizeof(buf),
                 "{\"ev\":\"%s\",\"temperature\":%.1f,\"humidity\":%.1f}",
                 name, (double)d.temperature, (double)d.humidity);
    } else {
        snprintf(buf, sizeof(buf), "{\"ev\":\"%s\"}", name);
    }
    web_ws_broadcast(buf, strlen(buf));
    return 0;
}

static void web_events_setup(void) {
    static const event_type_t k_watch[] = {
        EVENT_WIFI_CONNECTED,
        EVENT_WIFI_DISCONNECTED,
        EVENT_WIFI_GOT_IP,
        EVENT_WIFI_MODE_CHANGED,
        EVENT_SENSOR_TEMP_HUMIDITY,
        EVENT_STORAGE_ERROR,
        EVENT_SYSTEM_ERROR,
    };
    int ok = 0;
    for (size_t i = 0; i < sizeof(k_watch) / sizeof(k_watch[0]); i++) {
        if (event_bus_subscribe(k_watch[i], on_event_ws_push, NULL) != NULL) ok++;
    }
    LOGI("[WEB]", "event subscribers registered: %d/%d", ok,
         (int)(sizeof(k_watch) / sizeof(k_watch[0])));
}

static void register_builtin_routes(void) {
    web_route_t r;

    r = (web_route_t){"GET", "/api/wifi/scan", handle_wifi_scan};
    web_register_route(&r);
    r = (web_route_t){"GET", "/api/wifi/status", handle_wifi_status};
    web_register_route(&r);
    r = (web_route_t){"POST", "/api/wifi/connect", handle_wifi_connect};
    web_register_route(&r);
    r = (web_route_t){"POST", "/api/net/mode", handle_net_mode};
    web_register_route(&r);
    r = (web_route_t){"GET", "/api/status", handle_device_status};
    web_register_route(&r);
    r = (web_route_t){"GET", "/api/hello", handle_hello};
    web_register_route(&r);
    r = (web_route_t){"POST", "/api/web/update", handle_web_update};
    web_register_route(&r);
    r = (web_route_t){"GET", "/api/modules", handle_modules};
    web_register_route(&r);

    web_events_setup();   /* Phase 1: 事件总线真实订阅者（≥6） */
    web_ota_routes_init();
    web_api_sysinfo_init();   /* [WEB] 契约 I5 只读信息路由（additive） */
}

// ===========================================================================
// 流式路由接管（Mongoose 7.21 HDRS + detach 模型）
// ===========================================================================

/** 头部就绪：匹配流式路由则接管连接 */
static void stream_try_takeover(struct mg_connection *c,
                                struct mg_http_message *hm) {
    char uri[160];
    char method[16];
    snprintf(uri, sizeof(uri), "%.*s", (int)hm->uri.len, hm->uri.buf);
    snprintf(method, sizeof(method), "%.*s", (int)hm->method.len, hm->method.buf);

    for (int i = 0; i < s_stream_count; i++) {
        const web_stream_route_t *r = &s_stream_routes[i];
        if (strcmp(r->uri, uri) != 0) continue;
        if (strcmp(r->method, method) != 0) continue;

        int rc = r->on_hdrs(c, hm);   /* 拒绝时函数内已回复错误 */
        if (rc != 0) return;

        web_stream_ctx_t *ctx = mem_calloc(1, sizeof(*ctx));
        if (!ctx) {
            mg_http_reply(c, 500, "", "{\"error\":\"oom\"}");
            c->is_draining = 1;
            return;
        }
        ctx->route = r;
        c->fn_data = ctx;

        /* 消费头部字节 → http_cb 检测到 recv 被外部修改后自动 detach，
         * 此连接后续以裸 MG_EV_READ 流式交付给 web_event_handler */
        /* 头部与 body 的边界只看 "\r\n\r\n"：HDRS 事件的 hm->message.len
         * 可能包含已随头部提前到达的 body 前缀，直接删会吞掉固件开头 */
        const char *buf = (const char *)c->recv.buf;
        size_t hdr_len = hm->message.len;
        for (size_t i = 0; i + 4 <= c->recv.len; i++) {
            if (buf[i] == '\r' && buf[i + 1] == '\n' &&
                buf[i + 2] == '\r' && buf[i + 3] == '\n') {
                hdr_len = i + 4;
                break;
            }
        }
        mg_iobuf_del(&c->recv, 0, hdr_len);

        /* 头部之后可能已经到了一部分 body */
        stream_feed(ctx, c);
        return;
    }
}

/** 交付缓冲区内数据给流式路由 */
static void stream_feed(web_stream_ctx_t *ctx, struct mg_connection *c) {
    while (c->recv.len > 0) {
        size_t n = ctx->route->on_data(c, (const char *)c->recv.buf,
                                       c->recv.len);
        if (n == (size_t)-1) {
            /* 致命错误：handler 已回复响应，排空后关闭 */
            c->is_draining = 1;
            return;
        }
        if (n == 0) return;          /* 需要等待更多数据/暂停 */
        if (n > c->recv.len) return; /* 防御：越界消费 */
        mg_iobuf_del(&c->recv, 0, n);
    }
}

static void stream_on_close(web_stream_ctx_t *ctx, struct mg_connection *c) {
    if (ctx->route->on_close) {
        ctx->route->on_close(c);
    }
    mem_free(ctx);
}

// ===========================================================================
// WebSocket
// ===========================================================================

int web_ws_broadcast(const char *json, size_t len) {
    if (!s_ws_queue || !json || len == 0) return -1;
    ws_msg_t msg;
    msg.json = mem_malloc(len);
    if (!msg.json) return -1;
    memcpy(msg.json, json, len);
    msg.len = len;
    if (xQueueSend(s_ws_queue, &msg, 0) != pdTRUE) {
        mem_free(msg.json);
        return -1;
    }
    return 0;
}

static void ws_broadcast_status(void) {
    int len;
    char json[192];
    net_status_t st = {0};
    net_mgr_get_status(&st);

    len = snprintf(json, sizeof(json),
        "{\"type\":\"status\",\"uptime_sec\":%llu,\"rssi\":%d,"
        "\"free_heap\":%u,\"net_state\":\"%s\",\"ip\":\"%s\"}",
        (unsigned long long)heartbeat_get_uptime_sec(),
        heartbeat_get_rssi(),
        (unsigned)esp_get_free_heap_size(),
        net_state_to_str(st.state), st.ip);
    if (len <= 0 || len >= (int)sizeof(json)) return;

    struct ws_client **p = &s_ws_clients;
    while (*p) {
        struct ws_client *client = *p;
        if (client->c == NULL || client->c->is_closing ||
            client->c->is_resp == 0) {
            *p = client->next;
            mem_free(client);
        } else {
            mg_ws_send(client->c, json, len, WEBSOCKET_OP_TEXT);
            p = &client->next;
        }
    }
}

static void ws_remove_client(struct mg_connection *c) {
    struct ws_client **p = &s_ws_clients;
    while (*p) {
        if ((*p)->c == c) {
            struct ws_client *dead = *p;
            *p = dead->next;
            mem_free(dead);
            LOGI(TAG, "WebSocket client left");
            return;
        }
        p = &(*p)->next;
    }
}

static void handle_ws_upgrade(struct mg_connection *c,
                              struct mg_http_message *hm) {
    struct mg_str *upgrade = mg_http_get_header(hm, "Upgrade");
    if (upgrade == NULL || mg_strcasecmp(*upgrade, mg_str("websocket")) != 0) {
        mg_http_reply(c, 400, "", "bad request");
        return;
    }

    mg_ws_upgrade(c, hm, NULL);

    struct ws_client *client = mem_calloc(1, sizeof(*client));
    if (client) {
        client->c = c;
        client->next = s_ws_clients;
        s_ws_clients = client;
        LOGI(TAG, "WebSocket client connected");
    }
    ws_broadcast_status();
}

// ===========================================================================
// 事件分发
// ===========================================================================

static void web_event_handler(struct mg_connection *c, int ev, void *ev_data) {
    /* 已接管的流式连接：只走流式路径 */
    web_stream_ctx_t *ctx = (web_stream_ctx_t *)c->fn_data;
    if (ctx) {
        if (ev == MG_EV_READ) {
            stream_feed(ctx, c);
        } else if (ev == MG_EV_CLOSE) {
            stream_on_close(ctx, c);
        }
        return;
    }

    if (ev == MG_EV_HTTP_HDRS) {
        /* 大 body 请求在此接管流式处理（完整 MSG 事件会等收齐全部 body） */
        stream_try_takeover(c, (struct mg_http_message *)ev_data);
        return;
    }

    if (ev != MG_EV_HTTP_MSG) {
        if (ev == MG_EV_CLOSE) {
            ws_remove_client(c);
        }
        return;
    }

    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    char uri[160];
    char method[16];
    snprintf(uri, sizeof(uri), "%.*s", (int)hm->uri.len, hm->uri.buf);
    snprintf(method, sizeof(method), "%.*s", (int)hm->method.len, hm->method.buf);

    /* WebSocket 升级 */
    if (strcmp(uri, "/ws") == 0) {
        handle_ws_upgrade(c, hm);
        return;
    }

    /* 精确路由 */
    for (int i = 0; i < s_route_count; i++) {
        if (strcmp(s_routes[i].uri, uri) == 0 &&
            strcmp(s_routes[i].method, method) == 0) {
            s_routes[i].fn(c, hm);
            return;
        }
    }

    /* /api 前缀未匹配 → JSON 404；其余走静态文件 */
    if (strncmp(uri, "/api/", 5) == 0) {
        mg_http_reply(c, 404, "Content-Type: application/json\r\n",
                      "{\"error\":\"not found\"}");
        return;
    }
    serve_static_file(c, uri);
}

// ===========================================================================
// Web 任务
// ===========================================================================

/** tasker 周期任务：投递"推送状态"信号（json=NULL），由 web 任务执行广播 */
static enum task_t ws_push_tasker_fn(void *ctx) {
    (void)ctx;
    ws_msg_t msg = { .json = NULL, .len = 0 };
    if (s_ws_queue) xQueueSend(s_ws_queue, &msg, 0);
    return TASK_OK;
}

static void web_task(void *arg) {
    (void)arg;
    LOGI(TAG, "Web task running");

    esp_task_wdt_add(NULL);

    while (s_server_running) {
        esp_task_wdt_reset();

        /* 其他任务的 WS 请求在本线程安全执行（json=NULL = 推送状态） */
        ws_msg_t msg;
        while (s_ws_queue && xQueueReceive(s_ws_queue, &msg, 0) == pdTRUE) {
            if (msg.json == NULL) {
                ws_broadcast_status();
                continue;
            }
            struct ws_client **p = &s_ws_clients;
            while (*p) {
                struct ws_client *client = *p;
                if (client->c == NULL || client->c->is_closing ||
                    client->c->is_resp == 0) {
                    *p = client->next;
                    mem_free(client);
                } else {
                    mg_ws_send(client->c, msg.json, msg.len,
                               WEBSOCKET_OP_TEXT);
                    p = &client->next;
                }
            }
            mem_free(msg.json);
        }

        mg_mgr_poll(&s_mgr, 50);
        esp_task_wdt_reset();
    }

    esp_task_wdt_delete(NULL);
    mg_mgr_free(&s_mgr);
    LOGI(TAG, "Web task exited");
    s_web_task = NULL;
    vTaskDelete(NULL);
}

// ===========================================================================
// 启停
// ===========================================================================

/* Web 配网通道：web 服务常开，无独立启停动作 */
static net_err_t web_prov_start(void) { return NET_OK; }
static net_err_t web_prov_stop(void)  { return NET_OK; }
static const net_provision_provider_t s_web_prov = {
    .name = "web",
    .start = web_prov_start,
    .stop = web_prov_stop,
};

int web_server_start(void) {
    if (s_server_running) {
        LOGW(TAG, "Web server already running");
        return 0;
    }

    /* 配网通道注册（web 通道：REST API 收凭据） */
    net_provision_register(&s_web_prov);

    register_builtin_routes();

    mg_mgr_init(&s_mgr);

    char listen_addr[32];
    snprintf(listen_addr, sizeof(listen_addr), "http://0.0.0.0:%d", WEB_PORT);
    if (!mg_http_listen(&s_mgr, listen_addr, web_event_handler, NULL)) {
        LOGE(TAG, "Failed to listen on port %d", WEB_PORT);
        mg_mgr_free(&s_mgr);
        return -1;
    }

    s_ws_queue = xQueueCreate(WS_QUEUE_DEPTH, sizeof(ws_msg_t));
    if (!s_ws_queue) LOGE(TAG, "ws queue create failed");

    /* 每秒周期 WS 状态推送 */
    struct task_node ws_node;
    if (tasker_task_init_mi(&ws_node, 1000, -1, "ws_push",
                            ws_push_tasker_fn, NULL) == TASK_OK) {
        tasker_enqueue(&ws_node);
    }

    s_server_running = 1;
    if (xTaskCreate(web_task, "web", WEB_TASK_STACK, NULL,
                    WEB_TASK_PRIO, &s_web_task) != pdPASS) {
        LOGE(TAG, "Failed to create web task");
        s_server_running = 0;
        mg_mgr_free(&s_mgr);
        return -1;
    }

    LOGI(TAG, "Web server started on port %d", WEB_PORT);
    return 0;
}

void web_server_stop(void) {
    s_server_running = 0;
    LOGI(TAG, "Web server stopping...");
}
