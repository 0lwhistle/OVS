#ifndef WEB_DATA_H
#define WEB_DATA_H

#include <stddef.h>

// Web 资源版本哈希（用于 OTA 后判断是否需要更新 SPIFFS）
#define WEB_DATA_HASH "de659c4142cacc0f8c157534cfc9420b1b04a4e3522671035588bc141df7bb9d"

typedef struct {
    const char *path;       // 文件路径，如 "/index.html"
    const char *mime_type;  // MIME 类型
    const unsigned char *data;      // 文件数据
    size_t size;            // 文件大小
} web_file_t;

// 文件表
extern const web_file_t web_files[];
extern const int web_files_count;

#endif /* WEB_DATA_H */
