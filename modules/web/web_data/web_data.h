#ifndef WEB_DATA_H
#define WEB_DATA_H

#include <stddef.h>

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
