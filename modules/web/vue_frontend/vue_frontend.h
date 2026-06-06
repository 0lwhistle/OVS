#ifndef VUE_FRONTEND_H
#define VUE_FRONTEND_H

#include <stddef.h>

typedef struct {
    const char *path;       // 文件路径，如 "/index.html"
    const char *mime_type;  // MIME 类型，如 "text/html"
    const char *data;       // 文件内容
    size_t size;            // 文件大小
} vue_file_t;

// 文件表
extern const vue_file_t vue_files[];
extern const int vue_files_count;

#endif /* VUE_FRONTEND_H */
