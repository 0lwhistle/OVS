/**
 * @file net_types.h
 * @brief 网络模块公共类型（平台无关）
 */

#ifndef NET_TYPES_H
#define NET_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 网络模块错误码（正值成功，负值失败）
 */
typedef enum {
    NET_OK                = 0,   /**< 成功 */
    NET_ERR_INVALID_PARAM = -1,  /**< 参数无效 */
    NET_ERR_NOT_INITIALIZED = -2,/**< 模块未初始化 */
    NET_ERR_INVALID_STATE = -3,  /**< 当前状态不允许该操作 */
    NET_ERR_NVS           = -4,  /**< NVS 读写失败 */
    NET_ERR_WIFI          = -5,  /**< WiFi 驱动操作失败 */
    NET_ERR_TIMEOUT       = -6,  /**< 操作超时 */
    NET_ERR_NO_MEMORY     = -7,  /**< 内存不足 */
    NET_ERR_NOT_FOUND     = -8,  /**< 未找到（如配网通道未注册） */
    NET_ERR_FULL          = -9,  /**< 注册表已满 */
} net_err_t;

/**
 * @brief 错误码转可读字符串
 */
const char *net_err_to_str(net_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* NET_TYPES_H */
