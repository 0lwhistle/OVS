#include "web.h"
#include "web_data.h"       // 由 tools/fs_to_c.py 生成
#include "mongoose.h"
#include "ota.h"
#include "heartbeat.h"
#include "../devices_ctrl/wifi/wifi.h"


#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"


static const char *TAG = "[WEB]";

static struct mg_mgr s_mgr;
static struct mg_connection *s_listen_conn = NULL;
static int s_server_running = 0;

// ========== SPIFFS 初始化 & Web 资源解压 ==========


/**
 * @brief 确保目录存在（递归创建）
 */
static int ensure_dir(const char *path) {
    char tmp[256];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
    return 0;
}

/**
 * @brief 将数据直接写入文件（不压缩）
 */
static int write_data_to_file(const char *filepath,
                               const unsigned char *data,
                               size_t size) {
    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        ESP_LOGE(TAG, "fopen failed: %s", filepath);
        return -1;
    }

    if (size > 0 && fwrite(data, 1, size, fp) != size) {
        ESP_LOGE(TAG, "fwrite failed for %s", filepath);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

int web_spiffs_init(void) {
    ESP_LOGI(TAG, "Initializing SPIFFS...");

    // 配置 SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = WEB_SPIFFS_MOUNT,
        .partition_label = "spiffs",
        .max_files = 10,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return -1;
    }

    // 读取 SPIFFS 中的版本哈希文件
    char spiffs_hash[65] = {0};  // SHA256 = 64 字符 hex + null
    FILE *hf = fopen(WEB_SPIFFS_MOUNT "/.web_hash", "r");
    bool hash_mismatch = true;  // 默认认为不匹配（需要重新部署）
    if (hf) {
        size_t nread = fread(spiffs_hash, 1, 64, hf);
        spiffs_hash[nread] = '\0';
        // 去掉换行符
        size_t slen = strlen(spiffs_hash);
        while (slen > 0 && (spiffs_hash[slen-1] == '\n' || spiffs_hash[slen-1] == '\r')) {
            spiffs_hash[--slen] = '\0';
        }
        fclose(hf);

        if (strcmp(spiffs_hash, WEB_DATA_HASH) == 0) {
            hash_mismatch = false;  // 哈希匹配，无需更新
            ESP_LOGI(TAG, "SPIFFS web resources up-to-date (hash=%s)", spiffs_hash);
        } else {
            ESP_LOGI(TAG, "SPIFFS hash mismatch: stored=%s, expected=%s",
                     spiffs_hash, WEB_DATA_HASH);
        }
    } else {
        ESP_LOGI(TAG, "No .web_hash file found in SPIFFS");
    }

    // 如果哈希匹配，跳过部署
    if (!hash_mismatch) {
        return 0;
    }

    // 哈希不匹配：需要重新部署 web 资源
    ESP_LOGI(TAG, "Re-deploying web resources (hash mismatch)");

    // 先格式化 SPIFFS 分区（清空旧文件）
    ESP_LOGI(TAG, "Formatting SPIFFS...");
    esp_vfs_spiffs_unregister("spiffs");
    esp_spiffs_format("spiffs");
    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS remount failed: %s", esp_err_to_name(ret));
        return -1;
    }

    ESP_LOGI(TAG, "SPIFFS is empty, writing web resources...");

    // 写入所有 web 资源到 spiffs
    for (int i = 0; i < web_files_count; i++) {
        const web_file_t *f = &web_files[i];

        // 构建完整路径: /spiffs/index.html
        char fullpath[256];
        snprintf(fullpath, sizeof(fullpath), "%s%s", WEB_SPIFFS_MOUNT, f->path);

        // 确保目录存在
        char dirpath[256];
        strncpy(dirpath, fullpath, sizeof(dirpath) - 1);
        char *last_slash = strrchr(dirpath, '/');
        if (last_slash) {
            *last_slash = '\0';
            ensure_dir(dirpath);
        }

        ESP_LOGI(TAG, "  Writing: %s (%zu bytes)", f->path, f->size);

        if (write_data_to_file(fullpath, f->data, f->size) != 0) {
            ESP_LOGE(TAG, "Failed to write: %s", f->path);
            return -1;
        }
    }

    // 写入版本哈希文件（用于下次启动时判断是否需要重新部署）
    hf = fopen(WEB_SPIFFS_MOUNT "/.web_hash", "w");
    if (hf) {
        fprintf(hf, "%s\n", WEB_DATA_HASH);
        fclose(hf);
        ESP_LOGI(TAG, "  Written: .web_hash (%s)", WEB_DATA_HASH);
    } else {
        ESP_LOGE(TAG, "Failed to write .web_hash");
    }

    ESP_LOGI(TAG, "All web resources written to SPIFFS");
    return 0;
}

// ========== Mongoose Web Server ==========

/**
 * @brief 从文件路径获取 MIME 类型
 */
static const char *get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    if (strcasecmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcasecmp(ext, ".css") == 0) return "text/css; charset=utf-8";
    if (strcasecmp(ext, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcasecmp(ext, ".json") == 0) return "application/json";
    if (strcasecmp(ext, ".png") == 0) return "image/png";
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, ".gif") == 0) return "image/gif";
    if (strcasecmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcasecmp(ext, ".ico") == 0) return "image/x-icon";
    if (strcasecmp(ext, ".woff") == 0) return "font/woff";
    if (strcasecmp(ext, ".woff2") == 0) return "font/woff2";
    if (strcasecmp(ext, ".ttf") == 0) return "font/ttf";

    return "application/octet-stream";
}

/**
 * @brief 处理 API 请求
 */

// ---------- 简单 JSON 字符串解析（嵌入式，不依赖外部库） ----------
// 从 JSON 字符串中提取 "key":"value"，返回 value 的 mg_str
static struct mg_str json_get_str_val(struct mg_str json, const char *key) {
    struct mg_str result = {NULL, 0};

    // 构造搜索模式: "key":" 
    char pattern[64];
    int pat_len = snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    if (pat_len < 0 || pat_len >= (int)sizeof(pattern)) return result;

    // 在 JSON 中搜索 pattern
    const char *start = json.buf;
    const char *end = json.buf + json.len;
    const char *pos = start;

    while (pos < end - pat_len) {
        if (memcmp(pos, pattern, pat_len) == 0) {
            // 找到值起始位置
            const char *val_start = pos + pat_len;
            const char *val_end = val_start;
            while (val_end < end && *val_end != '"') val_end++;
            if (val_end > val_start) {
                result.buf = (char *)val_start;
                result.len = val_end - val_start;
            }
            return result;
        }
        pos++;
    }

    return result;
}

/**
 * @brief 处理 Wi-Fi 扫描请求（GET /api/wifi/scan）
 */
static void handle_wifi_scan(struct mg_connection *c) {
    wifi_ap_info_t aps[20];
    int count = wifi_scan_aps(aps, 20);

    if (count <= 0) {
        mg_http_reply(c, 200,
                      "Content-Type: application/json\r\n",
                      "{\"aps\":[],\"count\":0}");
        return;
    }

    // 手动构造 JSON，用 mg_http_reply 一次性发送
    // 预估: ["ssid":33+"rssi":6+"auth":1 ≈ 50 字符/项 + 头尾 ≈ 50*count + 50
    int est_len = 50 * count + 100;
    char *buf = malloc(est_len);
    if (!buf) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"OOM\"}");
        return;
    }

    int off = snprintf(buf, est_len, "{\"aps\":[");
    for (int i = 0; i < count; i++) {
        if (i > 0) off += snprintf(buf + off, est_len - off, ",");
        // 转义 SSID 中的特殊字符（简单处理：只处理双引号和反斜杠）
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

    mg_http_reply(c, 200,
                  "Content-Type: application/json\r\n",
                  "%.*s", off, buf);
    free(buf);
}

/**
 * @brief 处理 Wi-Fi 连接请求（POST /api/wifi/connect）
 */
static void handle_wifi_connect(struct mg_connection *c, struct mg_http_message *hm) {
    if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
        mg_http_reply(c, 405,
                      "Content-Type: application/json\r\n",
                      "{\"error\":\"method not allowed, use POST\"}");
        return;
    }

    // 解析 JSON body: {"ssid":"xxx","password":"xxx"}
    struct mg_str ssid_val = json_get_str_val(hm->body, "ssid");
    struct mg_str pass_val = json_get_str_val(hm->body, "password");

    if (ssid_val.len == 0 || ssid_val.len > 32) {
        mg_http_reply(c, 400,
                      "Content-Type: application/json\r\n",
                      "{\"error\":\"invalid ssid\"}");
        return;
    }
    if (pass_val.len == 0 || pass_val.len > 64) {
        mg_http_reply(c, 400,
                      "Content-Type: application/json\r\n",
                      "{\"error\":\"invalid password\"}");
        return;
    }

    char ssid[33] = {0};
    char password[65] = {0};
    memcpy(ssid, ssid_val.buf, ssid_val.len);
    memcpy(password, pass_val.buf, pass_val.len);

    ESP_LOGI(TAG, "WiFi connect request: SSID=%s", ssid);

    int ret = wifi_switch_ap(ssid, password);
    if (ret != 0) {
        mg_http_reply(c, 409,
                      "Content-Type: application/json\r\n",
                      "{\"error\":\"switch already in progress\"}");
        return;
    }

    mg_http_reply(c, 200,
                  "Content-Type: application/json\r\n",
                  "{\"status\":\"ok\",\"message\":\"switching\"}");
}

/**
 * @brief 处理 Wi-Fi 状态查询（GET /api/wifi/status）
 */
static void handle_wifi_status(struct mg_connection *c) {
    wifi_switch_status_t st;
    wifi_get_switch_status(&st);

    const char *state_str = "idle";
    switch (st.state) {
        case WIFI_STATE_IDLE:      state_str = "idle"; break;
        case WIFI_STATE_SCANNING:  state_str = "scanning"; break;
        case WIFI_STATE_SWITCHING: state_str = "switching"; break;
        case WIFI_STATE_CONNECTED: state_str = "connected"; break;
        case WIFI_STATE_FAILED:    state_str = "failed"; break;
    }

    mg_http_reply(c, 200,
                  "Content-Type: application/json\r\n",
                  "{\"state\":\"%s\","
                  "\"current_ssid\":\"%s\","
                  "\"new_ssid\":\"%s\","
                  "\"rssi\":%d,"
                  "\"elapsed_sec\":%d,"
                  "\"rollback_available\":%s}",
                  state_str,
                  st.current_ssid,
                  st.new_ssid,
                  st.rssi,
                  st.elapsed_sec,
                  st.rollback_available ? "true" : "false");
}

static void handle_api_call(struct mg_connection *c, const char *method,
                            const char *uri, struct mg_str body) {
    // Wi-Fi 扫描 API
    if (strcmp(uri, "/api/wifi/scan") == 0 && strcmp(method, "GET") == 0) {
        handle_wifi_scan(c);
        return;
    }

    // Wi-Fi 连接 API（POST 请求在 event_handler 中直接处理）

    // Wi-Fi 状态查询 API
    if (strcmp(uri, "/api/wifi/status") == 0 && strcmp(method, "GET") == 0) {
        handle_wifi_status(c);
        return;
    }

    // 示例 API: GET /api/hello
    if (strcmp(uri, "/api/hello") == 0 && strcmp(method, "GET") == 0) {
        mg_http_reply(c, 200,
                      "Content-Type: application/json\r\n",
                      "{\"message\":\"Hello from ESP32!\",\"status\":\"ok\"}");
        return;
    }

    // 示例 API: GET /api/status（使用心跳缓存的运行时间）
    if (strcmp(uri, "/api/status") == 0 && strcmp(method, "GET") == 0) {
        mg_http_reply(c, 200,
                      "Content-Type: application/json\r\n",
                      "{\"device\":\"ESP32-S3\",\"uptime_ms\":%" PRIu64 ","
                      "\"rssi\":%d}",
                      heartbeat_get_uptime_sec() * 1000,
                      heartbeat_get_rssi());
        return;
    }

    // 404
    mg_http_reply(c, 404,
                  "Content-Type: application/json\r\n",
                  "{\"error\":\"not found\"}");
}

/**
 * @brief 提供静态文件服务（从 spiffs 读取）
 */
static void serve_static_file(struct mg_connection *c, const char *uri) {
    // 默认首页
    const char *served_path = uri;
    if (strcmp(uri, "/") == 0) {
        served_path = "/index.html";
    }

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s%s", WEB_SPIFFS_MOUNT, served_path);

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        // 404
        mg_http_reply(c, 404,
                      "Content-Type: text/plain\r\n",
                      "404 Not Found");
        return;
    }

    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    const char *mime = get_mime_type(served_path);

    // 发送响应头
    mg_printf(c,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: %s\r\n"
              "Content-Length: %ld\r\n"
              "Connection: close\r\n"
              "\r\n",
              mime, fsize);

    // 发送文件内容（分块读取）
    char buf[512];
    size_t nread;
    while ((nread = fread(buf, 1, sizeof(buf), fp)) > 0) {
        mg_send(c, buf, nread);
    }

    fclose(fp);
}

/**
 * @brief 处理 OTA 固件上传（POST /ota/update）
 * 
 * 支持断点续传：
 * - 首次上传：POST 完整固件
 * - 续传：POST 剩余部分，通过 X-Offset 头指定偏移量
 * 
 * 客户端使用 curl -C - 会自动发送 Range 头，但这里用自定义 X-Offset 头更简单。
 * 客户端脚本：
 *   # 先查询进度
 *   offset=$(curl -s http://esp32/ota/progress | jq '.written')
 *   # 从断点处续传
 *   curl -X POST --data-binary @firmware.bin \
 *        -H "X-Offset: $offset" \
 *        http://esp32/ota/update
 */
static void handle_ota_update(struct mg_connection *c, struct mg_http_message *hm) {
    // 检查是否为 POST 方法
    if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
        mg_http_reply(c, 405,
                      "Content-Type: application/json\r\n",
                      "{\"error\":\"method not allowed, use POST\"}");
        return;
    }

    // 解析 X-Offset 头（断点续传偏移量）
    size_t resume_offset = 0;
    struct mg_str *x_offset = mg_http_get_header(hm, "X-Offset");
    if (x_offset != NULL) {
        char offset_str[32];
        size_t copy_len = x_offset->len;
        if (copy_len > sizeof(offset_str) - 1) copy_len = sizeof(offset_str) - 1;
        memcpy(offset_str, x_offset->buf, copy_len);
        offset_str[copy_len] = '\0';
        resume_offset = (size_t)atoll(offset_str);
    }


    ESP_LOGI(TAG, "OTA update request received, body: %zu bytes, offset: %zu",
             hm->body.len, resume_offset);

    // 1. 开始 OTA（支持续传）
    bool resume = (resume_offset > 0);
    esp_err_t err = ota_start(hm->body.len + resume_offset, resume);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_start failed: %s", esp_err_to_name(err));
        mg_http_reply(c, 500,
                      "Content-Type: application/json\r\n",
                      "{\"error\":\"ota_start failed: %s\"}",
                      esp_err_to_name(err));
        return;
    }

    // 2. 将 body 按 OTA_CHUNK_SIZE 分块推送到队列
    size_t offset = 0;
    while (offset < hm->body.len) {
        size_t chunk_len = hm->body.len - offset;
        if (chunk_len > OTA_CHUNK_SIZE) {
            chunk_len = OTA_CHUNK_SIZE;
        }

        err = ota_push_data(hm->body.buf + offset, chunk_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ota_push_data failed at offset %zu: %s",
                     offset, esp_err_to_name(err));
            ota_abort();
            mg_http_reply(c, 500,
                          "Content-Type: application/json\r\n",
                          "{\"error\":\"ota_push_data failed at offset %zu\"}",
                          offset);
            return;
        }

        offset += chunk_len;

        // 每处理一块数据喂一次狗，防止大文件上传时 watchdog 超时
        esp_task_wdt_reset();
    }

    ESP_LOGI(TAG, "All data pushed to OTA queue, finishing...");

    // 3. 结束 OTA（后台任务会完成校验并重启）
    mg_http_reply(c, 200,
                  "Content-Type: application/json\r\n",
                  "{\"status\":\"ok\",\"message\":\"OTA update success, rebooting...\"}");

    ota_finish();
}

/**
 * @brief 处理 OTA 进度查询（GET /ota/progress）
 * 
 * 返回当前 OTA 进度，用于断点续传查询偏移量。
 */
static void handle_ota_progress(struct mg_connection *c, struct mg_http_message *hm) {
    size_t written = ota_get_progress();
    size_t total = ota_get_total_size();

    mg_http_reply(c, 200,
                  "Content-Type: application/json\r\n",
                  "{\"written\":%u,\"total\":%u}",
                  (unsigned int)written, (unsigned int)total);
}




/**
 * @brief Mongoose 事件处理函数
 */

// ===================== 网页 OTA（SPIFFS 更新） =====================

/**
 * @brief 最小 tar 解析器：从 tar 数据中提取文件到 SPIFFS
 *
 * tar 格式（USTAR）：
 *   512 字节头 + 文件数据(padding 到 512B) + ... + 两个 512B 全零块结束
 * @param data  tar 数据指针
 * @param len   数据总长度
 * @return 提取的文件数量，<0 表示失败
 */
static int tar_extract_to_spiffs(const unsigned char *data, size_t len) {
    size_t offset = 0;
    int files_extracted = 0;
    int consecutive_zero_blocks = 0;

    while (offset + 512 <= len) {
        const unsigned char *header = data + offset;

        // 检测是否全零块（tar 结束标记 = 两个连续 512B 全零块）
        int is_zero = 1;
        for (int i = 0; i < 512; i++) {
            if (header[i] != 0) { is_zero = 0; break; }
        }
        if (is_zero) {
            consecutive_zero_blocks++;
            if (consecutive_zero_blocks >= 2) break;
            offset += 512;
            continue;
        }
        consecutive_zero_blocks = 0;

        // 解析文件名（位置 0-99）
        char filename[256];
        memcpy(filename, header, 100);
        filename[100] = '\0';
        // 去除尾部空格
        int fnlen = strlen(filename);
        while (fnlen > 0 && filename[fnlen-1] == ' ') filename[--fnlen] = '\0';
        if (fnlen == 0) { offset += 512; continue; }

        // 跳过目录条目（以 / 结尾）
        if (filename[fnlen-1] == '/') {
            // 创建目录
            char dirpath[256];
            snprintf(dirpath, sizeof(dirpath), "%s%.*s",
                     WEB_SPIFFS_MOUNT, fnlen, filename);
            // 去掉末尾的 /
            int dplen = strlen(dirpath);
            if (dplen > 0 && dirpath[dplen-1] == '/') dirpath[dplen-1] = '\0';
            mkdir(dirpath, 0755);
            offset += 512;
            continue;
        }

        // 解析文件大小（位置 124-135，八进制 ASCII）
        char size_str[13];
        memcpy(size_str, header + 124, 12);
        size_str[12] = '\0';
        size_t file_size = (size_t)strtoul(size_str, NULL, 8);

        // 跳过 512 字节头
        offset += 512;

        if (offset + file_size > len) {
            ESP_LOGE(TAG, "Tar: file %s exceeds data boundary", filename);
            return -1;
        }

        // 写入文件到 SPIFFS
        // 去除 tar 路径开头的 /（如 "/index.html" → "index.html"）
        const char *clean_name = filename;
        while (*clean_name == '/') clean_name++;
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", WEB_SPIFFS_MOUNT, clean_name);

        // 确保父目录存在
        char parent[512];
        snprintf(parent, sizeof(parent), "%s", filepath);
        char *last_slash = strrchr(parent, '/');
        if (last_slash) {
            *last_slash = '\0';
            mkdir(parent, 0755);
        }

        FILE *fp = fopen(filepath, "wb");
        if (!fp) {
            ESP_LOGE(TAG, "Tar: failed to create %s", filepath);
            return -1;
        }
        size_t written = fwrite(data + offset, 1, file_size, fp);
        fclose(fp);

        if (written != file_size) {
            ESP_LOGE(TAG, "Tar: write mismatch for %s (%zu/%zu)", filename, written, file_size);
            return -1;
        }

        files_extracted++;
        ESP_LOGI(TAG, "  Extracted: %s (%zu bytes)", clean_name, file_size);

        // 跳过文件数据 + padding 到 512 字节边界
        size_t padded = (file_size + 511) & ~511;
        offset += padded;
    }

    return files_extracted;
}

/**
 * @brief 处理网页 OTA 更新（POST /api/web/update）
 *
 * 接收 tar 格式的网页包，解压到 SPIFFS 分区。
 * 处理完成后需刷新浏览器缓存才能看到新页面。
 */
static void handle_web_update(struct mg_connection *c, struct mg_http_message *hm) {
    if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
        mg_http_reply(c, 405,
                      "Content-Type: application/json\r\n",
                      "{\"error\":\"method not allowed, use POST\"}");
        return;
    }

    size_t body_len = hm->body.len;
    if (body_len == 0) {
        mg_http_reply(c, 400,
                      "Content-Type: application/json\r\n",
                      "{\"error\":\"empty body\"}");
        return;
    }

    ESP_LOGI(TAG, "Web update: received %zu bytes", body_len);

    // 先删除旧的网页文件（清空 SPIFFS）
    ESP_LOGI(TAG, "Cleaning old web files...");
    DIR *dir = opendir(WEB_SPIFFS_MOUNT);
    if (dir) {
        struct dirent *entry;
        char fullpath[320];
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            snprintf(fullpath, sizeof(fullpath), "%s/%s", WEB_SPIFFS_MOUNT, entry->d_name);
            // 删除文件或目录
            struct stat st;
            if (stat(fullpath, &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    // 跳过目录清理（简单起见，只删顶层文件）
                    ESP_LOGI(TAG, "  Skip dir: %s", entry->d_name);
                } else {
                    unlink(fullpath);
                    ESP_LOGI(TAG, "  Deleted: %s", entry->d_name);
                }
            }
        }
        closedir(dir);
    }

    // 解析 tar 并提取文件
    ESP_LOGI(TAG, "Extracting web files from tar...");
    int count = tar_extract_to_spiffs((const unsigned char *)hm->body.buf, body_len);

    if (count < 0) {
        mg_http_reply(c, 500,
                      "Content-Type: application/json\r\n",
                      "{\"error\":\"tar extraction failed\"}");
        return;
    }

    ESP_LOGI(TAG, "Web update complete: %d files extracted", count);

    mg_http_reply(c, 200,
                  "Content-Type: application/json\r\n",
                  "{\"status\":\"ok\",\"files\":%d,"
                  "\"message\":\"web updated successfully, please refresh\"}",
                  count);
}

static void web_event_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        // 提取 URI
        char uri[256];
        snprintf(uri, sizeof(uri), "%.*s", (int)hm->uri.len, hm->uri.buf);

        // 网页 OTA 升级
        if (strcmp(uri, "/api/web/update") == 0) {
            handle_web_update(c, hm);
            return;
        }

        // Wi-Fi 连接 API（需要 POST body，在事件处理中直接处理）
        if (strcmp(uri, "/api/wifi/connect") == 0) {
            handle_wifi_connect(c, hm);
            return;
        }

        // OTA 升级路由
        if (strcmp(uri, "/ota/update") == 0) {
            handle_ota_update(c, hm);
            return;
        }

        // OTA 进度查询
        if (strcmp(uri, "/ota/progress") == 0) {
            handle_ota_progress(c, hm);
            return;
        }

        // API 路由

        if (strncmp(uri, "/api/", 5) == 0) {
            char method[16];
            snprintf(method, sizeof(method), "%.*s", (int)hm->method.len, hm->method.buf);
            handle_api_call(c, method, uri, hm->body);
            return;
        }

        // 静态文件
        serve_static_file(c, uri);
    }
}


int web_server_start(void) {
    if (s_server_running) {
        ESP_LOGW(TAG, "Web server already running");
        return 0;
    }

    mg_mgr_init(&s_mgr);

    // 监听端口
    char listen_addr[32];
    snprintf(listen_addr, sizeof(listen_addr), "http://0.0.0.0:%d", WEB_PORT);

    s_listen_conn = mg_http_listen(&s_mgr, listen_addr, web_event_handler, NULL);
    if (!s_listen_conn) {
        ESP_LOGE(TAG, "Failed to listen on port %d", WEB_PORT);
        mg_mgr_free(&s_mgr);
        return -1;
    }

    ESP_LOGI(TAG, "Web server started on port %d", WEB_PORT);
    s_server_running = 1;

    // 将当前任务注册到 watchdog
    esp_task_wdt_add(NULL);

    // 轮询循环（在单独的任务中运行）
    while (s_server_running) {
        esp_task_wdt_reset();     // 喂狗，在 mg_mgr_poll 之前
        mg_mgr_poll(&s_mgr, 50);  // 50ms 超时
        esp_task_wdt_reset();     // 喂狗，在 mg_mgr_poll 之后
    }







    mg_mgr_free(&s_mgr);
    return 0;
}

void web_server_stop(void) {
    s_server_running = 0;
    ESP_LOGI(TAG, "Web server stopping...");
}
