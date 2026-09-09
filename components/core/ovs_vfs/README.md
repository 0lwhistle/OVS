# OVS VFS 虚拟文件系统模块

## 概述

VFS (Virtual File System) 是 OVS 项目的核心虚拟文件系统管理模块，负责路径路由和挂载管理。

**设计原则**：完全解耦，通过回调函数表与具体存储实现交互。

## 架构

```
应用层
    │
    ├── fopen("/audio/music.bin")
    ├── fopen("/font/hzk16.bin")
    └── fopen("/config/settings.json")
           │
    ┌──────▼──────┐
    │  VFS 路由层  │  根据路径前缀分发
    └──────┬──────┘
           │
    ┌──────┴──────────────────────┐
    │                             │
    ▼                             ▼
┌─────────────┐           ┌─────────────┐
│  W25Q128    │           │  Internal   │
│  (w25q128)  │           │  Flash      │
│  注册回调   │           │  注册回调   │
└─────────────┘           └─────────────┘
```

## 目录结构

```
components/core/ovs_vfs/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── ovs_vfs.h              # VFS 管理器 API
│   └── ovs_vfs_block_dev.h    # 块设备回调接口
└── src/
    ├── vfs.c                  # VFS 管理器实现
    ├── vfs_block_dev.c        # 块设备注册表
    └── vfs_littlefs_adapter.c # LittleFS 适配层
```

## 核心接口

### 1. 块设备回调接口 (ovs_vfs_block_dev.h)

```c
typedef struct {
    vfs_bd_err_t (*read)(void* priv, void* buf, size_t size, size_t offset);
    vfs_bd_err_t (*write)(void* priv, const void* buf, size_t size, size_t offset);
    vfs_bd_err_t (*erase)(void* priv, size_t offset, size_t size);
    size_t (*get_size)(void* priv);
} vfs_block_dev_ops_t;

typedef struct {
    const char* name;
    const vfs_block_dev_ops_t* ops;
    size_t block_size;
    void* priv;
} vfs_block_dev_t;

vfs_bd_err_t vfs_block_dev_register(const vfs_block_dev_t* dev);
const vfs_block_dev_t* vfs_block_dev_get(const char* name);
```

### 2. VFS 管理器 API (ovs_vfs.h)

```c
vfs_err_t vfs_init(void);
vfs_err_t vfs_mount_littlefs(const char* virtual_path, const char* device_name,
                              size_t offset, size_t size, bool format_if_fail);
vfs_err_t vfs_unmount(const char* virtual_path);
bool vfs_is_mounted(const char* path);
void vfs_print_status(void);

// 内部 API（由 LittleFS 适配层调用）
vfs_err_t vfs_register_mount_point(const char* virtual_path, const char* device_name,
                                    size_t offset, size_t size);
```

### 3. 便捷 API

```c
// 卸载所有已挂载的文件系统
vfs_err_t vfs_unmount_all(void);

// 格式化指定挂载点
vfs_err_t vfs_format(const char* virtual_path);

// 通过路径获取挂载点信息
vfs_err_t vfs_get_mount_info_by_path(const char* path, vfs_mount_info_t* info);
```
## 使用示例

### 1. 注册块设备（由各存储模块实现）

```c
static const vfs_block_dev_ops_t s_w25q128_ops = {
    .read = w25q128_vfs_read,
    .write = w25q128_vfs_write,
    .erase = w25q128_vfs_erase,
    .get_size = w25q128_vfs_get_size,
};

void w25q128_register_vfs(void) {
    vfs_block_dev_t dev = {
        .name = "w25q128",
        .ops = &s_w25q128_ops,
        .block_size = 4096,
    };
    vfs_block_dev_register(&dev);
}
```

### 2. 挂载 LittleFS 文件系统

```c
#include "ovs_vfs.h"

void app_main(void) {
    // 初始化
    vfs_init();
    
    // 注册块设备
    w25q128_register_vfs();
    internal_flash_register_vfs("littlefs");
    
    // 挂载 LittleFS
    vfs_mount_littlefs("/audio", "w25q128", 0, 8 * 1024 * 1024, true);
    vfs_mount_littlefs("/font", "w25q128", 8 * 1024 * 1024, 8 * 1024 * 1024, true);
    
    // 使用标准 POSIX 接口
    FILE* f = fopen("/audio/music.bin", "rb");
}
```

### 3. 设备树配置自动挂载

挂载配置位于单棵树 `components/dtbs/config/ovs.dtb.json` 的 `vfs.mounts` 节点：

```json
{
    "vfs": {
        "mounts": [
            {"path": "/font",   "device": "w25q128", "offset": 0,       "size": 2097152, "format_if_fail": false},
            {"path": "/audio",  "device": "w25q128", "offset": 2097152, "size": 6291456, "format_if_fail": false},
            {"path": "/media",  "device": "w25q128", "offset": 8388608, "size": 8388608, "format_if_fail": false},
            {"path": "/config", "device": "internal", "offset": 0,      "size": 0,       "format_if_fail": false}
        ]
    }
}
```

## 存储分区示例

当前 `ovs.dtb.json` 中的 W25Q128 分区布局：

| 路径 | 设备 | 偏移 | 大小 | 用途 |
|------|------|------|------|------|
| /font | w25q128 | 0 | 2MB | 字库文件 |
| /audio | w25q128 | 2MB | 6MB | 音频文件 |
| /media | w25q128 | 8MB | 8MB | 媒体文件 |
| /config | internal | 0 | 0（整分区） | 配置文件（内部 flash littlefs 分区） |

## 编译依赖

- esp_littlefs
- esp_blockdev
- logger
- vfs (ESP-IDF)
- dtbs (设备树)

## 版本

- v1.0 (2026-09-07) - 初始版本，支持 LittleFS 挂载和设备树配置

- v1.1 (2026-09-07) - 添加便捷 API：vfs_unmount_all, vfs_format, vfs_get_mount_info_by_path
  - 修复：vfs_unmount 现在正确调用 esp_vfs_littlefs_unregister
  - 修复：vfs_mount_littlefs 挂载后自动同步到 VFS 挂载表
  - 改进：vfs_format 使用 esp_vfs_littlefs_format 替代 raw erase
