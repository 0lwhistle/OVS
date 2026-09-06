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

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 日志宏定义 ========== */

/**
 * @brief 信息级别日志 (绿色)
 * @param tag 日志标签
 * @param fmt 格式化字符串
 * @param ... 可变参数
 */
#define LOGI(tag, fmt, ...) printf("\033[32m%s: " fmt "\033[0m\n", (tag), ##__VA_ARGS__)

/**
 * @brief 警告级别日志 (黄色)
 * @param tag 日志标签
 * @param fmt 格式化字符串
 * @param ... 可变参数
 */
#define LOGW(tag, fmt, ...) printf("\033[33m%s: " fmt "\033[0m\n", (tag), ##__VA_ARGS__)

/**
 * @brief 错误级别日志 (红色)
 * @param tag 日志标签
 * @param fmt 格式化字符串
 * @param ... 可变参数
 */
#define LOGE(tag, fmt, ...) printf("\033[31m%s: " fmt "\033[0m\n", (tag), ##__VA_ARGS__)

/**
 * @brief 调试级别日志 (蓝色)
 * @param tag 日志标签
 * @param fmt 格式化字符串
 * @param ... 可变参数
 */
#define LOGD(tag, fmt, ...) printf("\033[34m%s: " fmt "\033[0m\n", (tag), ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* LOGGER_H */
