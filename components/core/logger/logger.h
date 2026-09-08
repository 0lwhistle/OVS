#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * 日志等级过滤：等级数值越小越详细，低于全局等级的日志不打印。
 *   ERROR   只打印 LOGE
 *   WARNING 打印 LOGE / LOGW
 *   INFO    全部打印（默认，LOGI/LOGW/LOGE/LOGD）
 * 可编译期默认（LOGGER_DEFAULT_LEVEL）+ 运行期 logger_set_level() 调整。
 */
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_NONE  = 4,    /* 全部静音 */
} log_level_t;

/* 运行期设置/查询全局日志等级（越界值会被钳位到有效范围） */
void logger_set_level(log_level_t level);
log_level_t logger_get_level(void);

/* 供 LOGx 宏调用：判断该等级当前是否输出 */
int logger_level_enabled(log_level_t level);

#ifndef LOGGER_DEFAULT_LEVEL
#define LOGGER_DEFAULT_LEVEL LOG_LEVEL_INFO
#endif

#define _LOG_PRINT(lvl, color, tag, fmt, ...)                          \
    do {                                                               \
        if (logger_level_enabled(lvl)) {                               \
            printf("\033[" color "m%s: " fmt "\033[0m\n",              \
                   (tag), ##__VA_ARGS__);                              \
        }                                                              \
    } while (0)

#define LOGE(tag, fmt, ...) _LOG_PRINT(LOG_LEVEL_ERROR, "31", (tag), fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) _LOG_PRINT(LOG_LEVEL_WARN,  "33", (tag), fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) _LOG_PRINT(LOG_LEVEL_INFO,  "32", (tag), fmt, ##__VA_ARGS__)
/** Debug级别日志 (灰色)，INFO 等级下输出，WARNING 及以上静音 */
#define LOGD(tag, fmt, ...) _LOG_PRINT(LOG_LEVEL_DEBUG, "90", (tag), fmt, ##__VA_ARGS__)

#endif // LOGGER_H
