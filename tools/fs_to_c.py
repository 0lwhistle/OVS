#!/usr/bin/env python3
"""
将 Vue 构建的 dist/ 目录下的所有文件打包成 C 字节数组（不压缩）。
固件启动时直接写入 spiffs 分区。

用法: python fs_to_c.py <dist目录> <输出目录>
示例: python fs_to_c.py vue_web/vue-ui/dist modules/web/web_data
"""

import os
import sys

def generate_c_files(dist_dir, output_dir):
    """遍历 dist 目录，生成 C 文件（不压缩）"""
    
    # 收集所有文件
    files = []
    for root, dirs, filenames in os.walk(dist_dir):
        for f in filenames:
            filepath = os.path.join(root, f)
            relpath = os.path.relpath(filepath, dist_dir)
            files.append((relpath, filepath))
    
    # 生成 .h 文件
    h_content = '''#ifndef WEB_DATA_H
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
'''
    
    # 生成 .c 文件
    c_content = '#include "web_data.h"\n\n'
    
    total_size = 0
    
    # 为每个文件生成字节数组
    for relpath, filepath in files:
        with open(filepath, 'rb') as f:
            data = f.read()
        
        total_size += len(data)
        
        # 生成合法的 C 变量名
        varname = 'file_' + ''.join(c if c.isalnum() else '_' for c in relpath)
        
        # 写入字节数组
        c_content += f'/* {relpath} ({len(data)} bytes) */\n'
        c_content += f'static const unsigned char {varname}[] = {{\n'
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
            c_content += f'    {hex_bytes},\n'
        c_content += '};\n\n'
    
    # 生成文件表
    c_content += 'const web_file_t web_files[] = {\n'
    for relpath, filepath in files:
        varname = 'file_' + ''.join(c if c.isalnum() else '_' for c in relpath)
        
        # 根据扩展名确定 MIME 类型
        ext = os.path.splitext(relpath)[1].lower()
        mime_map = {
            '.html': 'text/html; charset=utf-8',
            '.css': 'text/css; charset=utf-8',
            '.js': 'application/javascript; charset=utf-8',
            '.json': 'application/json',
            '.png': 'image/png',
            '.jpg': 'image/jpeg',
            '.jpeg': 'image/jpeg',
            '.gif': 'image/gif',
            '.svg': 'image/svg+xml',
            '.ico': 'image/x-icon',
            '.woff': 'font/woff',
            '.woff2': 'font/woff2',
            '.ttf': 'font/ttf',
        }
        mime = mime_map.get(ext, 'application/octet-stream')
        
        c_content += f'    {{"/{relpath}", "{mime}", {varname}, sizeof({varname})}},\n'
    
    c_content += '};\n\n'
    c_content += f'const int web_files_count = {len(files)};\n'
    
    # 写入文件
    os.makedirs(output_dir, exist_ok=True)
    
    with open(os.path.join(output_dir, 'web_data.h'), 'w') as f:
        f.write(h_content)
    
    with open(os.path.join(output_dir, 'web_data.c'), 'w') as f:
        f.write(c_content)
    
    print(f"✅ 成功生成 {len(files)} 个文件:")
    print(f"   总大小: {total_size} bytes")
    for relpath, _ in files:
        print(f"   📄 /{relpath}")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("用法: python fs_to_c.py <dist目录> <输出目录>")
        print("示例: python fs_to_c.py vue_web/vue-ui/dist modules/web/web_data")
        sys.exit(1)
    
    generate_c_files(sys.argv[1], sys.argv[2])
