/**
 * @file logger.h
 * @brief 日志系统公共 API
 * 
 * 提供彩色日志输出功能。
 * 外部模块只需包含此头文件即可使用日志功能。
 */
#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define DEBUG 1

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 日志宏定义（语句版） ========== */

/**
 * @brief 信息级别日志 (绿色)
 */
#define LOGI(tag, fmt, ...) \
    do { if (DEBUG) printf("\033[32m%s: " fmt "\033[0m\n", (tag), ##__VA_ARGS__); } while(0)

/**
 * @brief 警告级别日志 (黄色)
 */
#define LOGW(tag, fmt, ...) \
    do { if (DEBUG) printf("\033[33m%s: " fmt "\033[0m\n", (tag), ##__VA_ARGS__); } while(0)

/**
 * @brief 错误级别日志 (红色)
 */
#define LOGE(tag, fmt, ...) \
    do { if (DEBUG) printf("\033[31m%s: " fmt "\033[0m\n", (tag), ##__VA_ARGS__); } while(0)

/**
 * @brief 调试级别日志 (蓝色)
 */
#define LOGD(tag, fmt, ...) \
    do { printf("\033[34m%s: " fmt "\033[0m\n", (tag), ##__VA_ARGS__); } while(0)

/* ========== 表达式版日志宏（用于三元运算符等） ========== */

/**
 * @brief 信息级别日志 - 表达式版
 */
#define LOGI_EXPR(tag, fmt, ...) \
    do { if (DEBUG) printf("\033[32m%s: " fmt "\033[0m\n", (tag), ##__VA_ARGS__); } while(0)

/**
 * @brief 警告级别日志 - 表达式版
 */
#define LOGW_EXPR(tag, fmt, ...) \
    do { if (DEBUG) printf("\033[33m%s: " fmt "\033[0m\n", (tag), ##__VA_ARGS__); } while(0)

/**
 * @brief 错误级别日志 - 表达式版
 */
#define LOGE_EXPR(tag, fmt, ...) \
    do { if (DEBUG) printf("\033[31m%s: " fmt "\033[0m\n", (tag), ##__VA_ARGS__); } while(0)

/**
 * @brief 调试级别日志 - 表达式版
 */
#define LOGD_EXPR(tag, fmt, ...) \
    do { printf("\033[34m%s: " fmt "\033[0m\n", (tag), ##__VA_ARGS__); } while(0)

#ifdef __cplusplus
}
#endif

#endif /* LOGGER_H */
