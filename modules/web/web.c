#include "web.h"
#include "web_data.h"       // 由 tools/fs_to_c.py 生成
#include "mongoose.h"

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

static const char *TAG = "[WEB]";

static struct mg_mgr s_mgr;
static struct mg_connection *s_listen_conn = NULL;
static int s_server_running = 0;

// ========== SPIFFS 初始化 & Web 资源解压 ==========

/**
 * @brief 检查 spiffs 分区是否为空（没有文件）
 */
static int spiffs_is_empty(void) {
    DIR *dir = opendir(WEB_SPIFFS_MOUNT);
    if (!dir) return 1;  // 无法打开视为空

    int empty = 1;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // 跳过 . 和 ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        empty = 0;
        break;
    }
    closedir(dir);
    return empty;
}

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

    // 检查分区是否为空
    if (!spiffs_is_empty()) {
        ESP_LOGI(TAG, "SPIFFS already has data, skip decompress");
        return 0;
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
static void handle_api_call(struct mg_connection *c, const char *method,
                            const char *uri, struct mg_str body) {
    // 示例 API: GET /api/hello
    if (strcmp(uri, "/api/hello") == 0 && strcmp(method, "GET") == 0) {
        mg_printf(c,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: application/json\r\n"
                  "Connection: close\r\n"
                  "\r\n"
                  "{\"message\":\"Hello from ESP32!\",\"status\":\"ok\"}");
        return;
    }

    // 示例 API: GET /api/status
    if (strcmp(uri, "/api/status") == 0 && strcmp(method, "GET") == 0) {
        mg_printf(c,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: application/json\r\n"
                  "Connection: close\r\n"
                  "\r\n"
                  "{\"device\":\"ESP32-S3\",\"uptime_ms\":%" PRIu64 "}",
                  (uint64_t)(esp_timer_get_time() / 1000));
        return;
    }

    // 404
    mg_printf(c,
              "HTTP/1.1 404 Not Found\r\n"
              "Content-Type: application/json\r\n"
              "Connection: close\r\n"
              "\r\n"
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
        mg_printf(c,
                  "HTTP/1.1 404 Not Found\r\n"
                  "Content-Type: text/plain\r\n"
                  "Connection: close\r\n"
                  "\r\n"
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
 * @brief Mongoose 事件处理函数
 */
static void web_event_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        // 提取 URI 和方法
        char uri[256];
        char method[16];
        snprintf(uri, sizeof(uri), "%.*s", (int)hm->uri.len, hm->uri.buf);
        snprintf(method, sizeof(method), "%.*s", (int)hm->method.len, hm->method.buf);

        // API 路由
        if (strncmp(uri, "/api/", 5) == 0) {
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

    // 轮询循环（在单独的任务中运行）
    while (s_server_running) {
        mg_mgr_poll(&s_mgr, 50);  // 50ms 超时
    }

    mg_mgr_free(&s_mgr);
    return 0;
}

void web_server_stop(void) {
    s_server_running = 0;
    ESP_LOGI(TAG, "Web server stopping...");
}
