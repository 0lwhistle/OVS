#include "mongoose.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi.h"
#include "led_ctrl.h"
#include "vue_frontend.h"       // ← 新增：引入 Vue 文件表

#define LED_CTRL_GPIO 4

// ---------- Mongoose 全局变量 ----------
static struct mg_mgr mgr;          // 事件管理器
static int s_blynk = 0;            // 板载LED状态（示例）

// ---------- 新增：根据路径查找对应的 Vue 文件 ----------
static const vue_file_t *find_vue_file(const char *path) {
    // 如果请求根路径，返回 index.html
    if (strcmp(path, "/") == 0 || strlen(path) == 0) {
        path = "/index.html";
    }
    
    for (int i = 0; i < vue_files_count; i++) {
        if (strcmp(vue_files[i].path, path) == 0) {
            return &vue_files[i];
        }
    }
    return NULL;
}

// ---------- HTTP 请求处理回调 ----------
static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        // ========== API 路由 ==========
        if (mg_match(hm->uri, mg_str("/api/toggle"), NULL)) {
            // 切换LED状态
            s_blynk = !s_blynk;
			if (led_ctrl_toggle(LED_CTRL_GPIO) < 0)
				printf("toggle fail\n");
            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                          "{\"led\": %d}", s_blynk);
        } else if (mg_match(hm->uri, mg_str("/api/status"), NULL)) {
            // 返回当前状态
            mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                          "{\"led\": %d, \"uptime\": %lu}",
                          s_blynk, (unsigned long)mg_millis());
        } 
        // ========== 静态文件路由（返回 Vue 页面） ==========
        else {
            // 从请求 URI 中提取路径
            char path[256];
            int len = hm->uri.len;
            if (len >= sizeof(path)) len = sizeof(path) - 1;
            memcpy(path, hm->uri.buf, len);
            path[len] = '\0';
            
            // 查找对应的 Vue 文件
            const vue_file_t *file = find_vue_file(path);
            if (file) {
                // 找到文件，返回它
                char headers[256];
                snprintf(headers, sizeof(headers),
                         "Content-Type: %s\r\n"
                         "Cache-Control: max-age=3600\r\n",
                         file->mime_type);
                
                mg_http_reply(c, 200, headers, "%.*s",
                             (int)file->size, file->data);
                printf("Served: %s (%zu bytes)\n", file->path, file->size);
            } else {
                // 文件不存在，返回 404
                mg_http_reply(c, 404, "Content-Type: text/plain\r\n",
                             "404 Not Found: %s", path);
            }
        }
    }
}

// ---------- 主函数入口 ----------
void app_main(void) {
    // 1. 初始化 NVS（Wi-Fi 需要）
    nvs_flash_init();

    // 2. 连接 Wi-Fi
    wifi_init();

	led_ctrl_init(LED_CTRL_GPIO, 1);

    // 3. 等待 Wi-Fi 连接（实际开发中建议用事件通知机制）
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 4. 启动 Mongoose
    mg_mgr_init(&mgr);

    // 5. 监听 80 端口，启动 HTTP 服务器
    mg_http_listen(&mgr, "http://0.0.0.0:80", fn, NULL);

    printf("Mongoose HTTP server started on port 80\n");
    printf("Vue frontend files loaded: %d\n", vue_files_count);

    // 6. 主循环：不停地轮询 Mongoose 事件
    while (1) {
        mg_mgr_poll(&mgr, 1000); // 每 1000ms 轮询一次
    }
}
