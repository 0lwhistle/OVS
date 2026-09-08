/**
 * @file web.h
 * @brief Web 服务模块（Mongoose 单任务事件驱动）
 *
 * 职责：
 *   - HTTP 监听与任务管理（内部自建 web 任务）
 *   - 路由注册表：各功能以 web_register_route()/web_register_stream_route()
 *     注册端点，web 核心不认识具体业务
 *   - 静态文件服务（/spiffs）、WebSocket 升级与广播
 *   - SPIFFS web 资源部署（固件内嵌资源 + hash 比对）
 *
 * 两种路由：
 *   - EXACT：完整请求收齐后回调（常规 REST API）
 *   - STREAM：大文件上传。MG_EV_HTTP_HDRS 时回调 on_hdrs，返回 0 表示接管
 *     连接（web 核心消费掉头部，Mongoose 退出 HTTP 解析）；之后每批数据经
 *     on_data 交付（返回消费的字节数，(size_t)-1 表示致命错误），连接关闭
 *     时回调 on_close。内存占用与传输总大小无关。
 *
 * 线程模型：唯一网络线程为内部 web 任务，所有回调在该任务上下文执行，
 * 同一时刻只有一个 handler 运行，天然互斥。
 * web_ws_broadcast() 可在其他任务调用（内部经队列桥接到 web 任务）。
 */

#ifndef WEB_H
#define WEB_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mg_connection;
struct mg_http_message;

#define WEB_PORT 80
// SPIFFS 挂载路径（静态资源）
#define WEB_SPIFFS_MOUNT "/spiffs"

// ---------------------------------------------------------------------------
// 路由注册
// ---------------------------------------------------------------------------

typedef void (*web_route_fn)(struct mg_connection *c,
                             struct mg_http_message *hm);

typedef enum {
    WEB_ROUTE_EXACT = 0,  /* MG_EV_HTTP_MSG 时精确匹配 method+uri */
} web_route_type_t;

typedef struct {
    const char *method;   /* "GET" / "POST" ... */
    const char *uri;      /* 精确路径，如 "/api/status" */
    web_route_fn   fn;
} web_route_t;

/** 流式上传路由（大 body；如 OTA 固件） */
typedef struct web_stream_route {
    const char *method;
    const char *uri;
    /** 头部就绪：返回 0 = 接管连接；非 0 = 拒绝（函数内自行回复错误） */
    int (*on_hdrs)(struct mg_connection *c, struct mg_http_message *hm);
    /** 数据交付：返回消费字节数；0 = 暂时等待；(size_t)-1 = 致命错误
     *  （错误响应由本函数自行回复） */
    size_t (*on_data)(struct mg_connection *c, const char *data, size_t len);
    /** 连接关闭（客户端中断或已完成），无论成败都会调用 */
    void (*on_close)(struct mg_connection *c);
} web_stream_route_t;

#define WEB_MAX_ROUTES 24

/**
 * @brief 注册精确路由（重复 method+uri 返回 -1；表满返回 -2）
 */
int web_register_route(const web_route_t *route);

/**
 * @brief 注册流式路由（uri 重复返回 -1；表满返回 -2）
 */
int web_register_stream_route(const web_stream_route_t *route);

// ---------------------------------------------------------------------------
// 服务生命周期
// ---------------------------------------------------------------------------

/**
 * @brief 初始化 SPIFFS 并按 hash 部署固件内嵌 web 资源
 *
 * 若 SPIFFS 已被挂载（如 main 已注册）则跳过挂载，仅做 hash 部署检查。
 * @return 0 成功，-1 失败
 */
int web_spiffs_init(void);

/**
 * @brief 启动 Web 服务器（内部自建任务，立即返回）
 * @return 0 成功，-1 失败
 */
int web_server_start(void);

/**
 * @brief 请求停止 Web 服务器（任务自行退出）
 */
void web_server_stop(void);

// ---------------------------------------------------------------------------
// WebSocket 广播（其他任务可调用）
// ---------------------------------------------------------------------------

/**
 * @brief 向所有 WebSocket 客户端广播一段 JSON 文本（内部拷贝，线程安全）
 * @return 0 已入队，-1 队列满/服务未启动
 */
int web_ws_broadcast(const char *json, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* WEB_H */
