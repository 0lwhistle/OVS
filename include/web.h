/**
 * @file web.h
 * @brief Web 服务器模块公共 API
 * 
 * 提供 HTTP 服务器功能，支持 REST API 和静态文件服务。
 */
#ifndef WEB_H
#define WEB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Web 常量定义 ========== */

#define WEB_SPIFFS_MOUNT "/spiffs"  /**< SPIFFS 挂载路径 */
#define WEB_PORT         80         /**< Web 服务器端口 */

/* ========== Web 公共 API ========== */

/**
 * @brief 初始化 SPIFFS 并解压 web 资源
 * 
 * 检测 spiffs 分区是否为空，如果为空则从固件中解压 web 资源。
 * 如果已有内容则跳过。
 * 
 * @return 0 成功，-1 失败
 */
int web_spiffs_init(void);

/**
 * @brief 启动 Mongoose Web 服务器
 * 
 * 提供 REST API (/api/...) 和静态文件服务 (/...)
 * 
 * @return 0 成功，-1 失败
 */
int web_server_start(void);

/**
 * @brief 停止 Web 服务器
 */
void web_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* WEB_H */
