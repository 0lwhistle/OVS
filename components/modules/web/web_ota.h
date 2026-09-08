/**
 * @file web_ota.h
 * @brief Web 模块内置 OTA 端点（内部头，web.c 启动时调用注册）
 */

#ifndef WEB_OTA_H
#define WEB_OTA_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册 OTA 上传/状态端点到路由表
 * @return 0 成功，-1 注册失败
 */
int web_ota_routes_init(void);

#ifdef __cplusplus
}
#endif

#endif /* WEB_OTA_H */
