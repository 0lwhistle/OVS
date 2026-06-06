#include "mongoose.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi.h"
#include "led_ctrl.h"

#define LED_CTRL_GPIO 4




// ---------- Mongoose 全局变量 ----------
static struct mg_mgr mgr;          // 事件管理器
static int s_blynk = 0;            // 板载LED状态（示例）

// ---------- HTTP 请求处理回调 ----------
static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        // 根据请求的URL返回不同内容
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
        } else {
            // 返回主页面（HTML）
            mg_http_reply(c, 200, "Content-Type: text/html\r\n",
                          "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                          "<title>ESP32 Web</title></head><body>"
                          "<h1>ESP32 Mongoose Web</h1>"
                          "<p>LED Status: <span id='led'>%s</span></p>"
                          "<button onclick='toggle()'>Toggle LED</button>"
                          "<script>"
                          "function toggle(){"
                          "fetch('/api/toggle').then(r=>r.json()).then(d=>{"
                          "document.getElementById('led').innerText=d.led?'ON':'OFF';"
                          "});}"
                          "fetch('/api/status').then(r=>r.json()).then(d=>{"
                          "document.getElementById('led').innerText=d.led?'ON':'OFF';"
                          "});"
                          "</script></body></html>",
                          s_blynk ? "ON" : "OFF");
        }
    }
}


// ---------- 主函数入口 ----------
void app_main(void) {
    // 1. 初始化 NVS（Wi-Fi 需要）
    nvs_flash_init();

    // 2. 连接 Wi-Fi
    wifi_init();

    // 3. 等待 Wi-Fi 连接（实际开发中建议用事件通知机制）
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 4. 启动 Mongoose
    mg_mgr_init(&mgr);

    // 5. 监听 80 端口，启动 HTTP 服务器
    mg_http_listen(&mgr, "http://0.0.0.0:80", fn, NULL);

    printf("Mongoose HTTP server started on port 80\n");

    // 6. 主循环：不停地轮询 Mongoose 事件
    while (1) {
        mg_mgr_poll(&mgr, 1000); // 每 1000ms 轮询一次
    }
}
