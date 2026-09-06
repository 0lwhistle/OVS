#!/usr/bin/env python3
"""
将 Vue dist 目录打包为 tar 格式，用于网页 OTA 升级。

用法:
    python scripts/pack_web.py <dist_dir> [output_path]

示例:
    python scripts/pack_web.py web/vue-ui/dist
    python scripts/pack_web.py web/vue-ui/dist build/web_update.tar

输出:
    build/web_update.tar (默认)
"""

import os
import sys
import tarfile
import io


def pack_web(dist_dir, output_path):
    """将 dist 目录打包为 tar（无压缩，ESP32 端手动解析）"""
    
    if not os.path.isdir(dist_dir):
        print(f"Error: {dist_dir} is not a directory")
        sys.exit(1)
    
    files = []
    total_size = 0
    for root, dirs, filenames in os.walk(dist_dir):
        for f in filenames:
            filepath = os.path.join(root, f)
            relpath = os.path.relpath(filepath, dist_dir)
            fsize = os.path.getsize(filepath)
            files.append((relpath, filepath, fsize))
            total_size += fsize
    
    if not files:
        print(f"Error: No files found in {dist_dir}")
        sys.exit(1)
    
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    
    with open(output_path, 'wb') as outf:
        tar_buffer = io.BytesIO()
        with tarfile.open(fileobj=tar_buffer, mode='w') as tar:
            for relpath, filepath, fsize in files:
                # tar 内部路径用 unix 风格，以 / 开头
                arcname = '/' + relpath.replace('\\', '/')
                tar.add(filepath, arcname=arcname)
        
        tar_data = tar_buffer.getvalue()
        outf.write(tar_data)
    
    tar_size = os.path.getsize(output_path)
    
    print(f"Packed {len(files)} files into {output_path}")
    print(f"  Total data: {total_size} bytes ({total_size/1024:.1f} KB)")
    print(f"  Tar size:   {tar_size} bytes ({tar_size/1024:.1f} KB)")
    print(f"  Overhead:   {tar_size - total_size} bytes")
    print()
    for relpath, _, fsize in files:
        print(f"  {relpath} ({fsize} bytes)")
    print()
    print(f"Upload via OTA page or:")
    print(f"  curl -X POST --data-binary @{output_path} http://<esp32-ip>/api/web/update")


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python scripts/pack_web.py <dist_dir> [output_path]")
        print("Example: python scripts/pack_web.py web/vue-ui/dist build/web_update.tar")
        sys.exit(1)
    
    dist_dir = sys.argv[1]
    output = sys.argv[2] if len(sys.argv) > 2 else 'build/web_update.tar'
    pack_web(dist_dir, output)
