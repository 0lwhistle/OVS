# VFS 虚拟文件系统设计方案

## 一、项目当前状态

### 已完成模块
1. **W25Q128 驱动** - SPI 通信，JEDEC ID 读取，擦除/写入/读取
2. **SPI 驱动** - DMA 支持，多设备共享总线
3. **设备树 (dtree)** - JSON 配置，从 SPIFFS 加载
4. **Holder 模块** - 模块生命周期管理

### 硬件配置
- ESP32-S3，16MB Flash，8MB PSRAM
- W25Q128 外部 Flash (16MB)，SPI 40MHz，引脚：SCLK=42, MISO=41, MOSI=40, CS=13
- SPI 屏幕 (ST7789) 共享 SPI 总线

### 性能测试结果
- W25Q128 @ 40MHz：写入 447 KB/s，读取 4357 KB/s
- 4KB 传输耗时 0.88ms

---

## 二、VFS 方案设计

### 架构图

```
应用程序
    │
    ├── fopen("/audio/music.bin")   ← 统一路径接口
    ├── fopen("/font/hzk16.bin")
    └── fopen("/config/settings.json")
           │
    ┌──────▼──────┐
    │  VFS 路由层  │  根据路径前缀路由到不同存储
    └──────┬──────┘
           │
    ┌──────┴──────────────────────┐
    │                             │
    ▼                             ▼
┌─────────────┐           ┌─────────────┐
│  内部Flash   │           │  W25Q128    │
│  /internal  │           │  /external  │
└─────────────┘           └─────────────┘
```

### 模块目录结构

```
components/
└── vfs/                            # VFS 组件
    ├── CMakeLists.txt
    ├── include/
    │   ├── vfs_block_dev.h         # 块设备抽象接口
    │   ├── vfs_manager.h           # VFS 管理器接口
    │   └── vfs_config.h            # 配置结构
    ├── src/
    │   ├── vfs_block_dev.c         # 块设备管理
    │   ├── vfs_manager.c           # VFS 路由逻辑
    │   ├── vfs_dev_internal.c      # 内部Flash块设备
    │   └── vfs_dev_w25q128.c       # W25Q128块设备
    └── config/
        └── vfs_config.json         # 挂载配置
```

---

## 三、核心接口设计

### 1. 块设备抽象接口 (vfs_block_dev.h)

```c
#ifndef VFS_BLOCK_DEV_H
#define VFS_BLOCK_DEV_H

#include "esp_err.h"
#include <stddef.h>

typedef struct vfs_block_dev_t vfs_block_dev_t;

/**
 * @brief 块设备操作接口
 */
struct vfs_block_dev_t {
    const char* name;
    
    /**
     * @brief 读取数据
     * @param dev 设备句柄
     * @param buf 读取缓冲区
     * @param size 读取大小
     * @param offset 起始偏移
     * @return esp_err_t 
     */
    esp_err_t (*read)(vfs_block_dev_t* dev, void* buf, size_t size, size_t offset);
    
    /**
     * @brief 写入数据
     * @param dev 设备句柄
     * @param buf 写入数据
     * @param size 写入大小
     * @param offset 起始偏移
     * @return esp_err_t 
     */
    esp_err_t (*write)(vfs_block_dev_t* dev, const void* buf, size_t size, size_t offset);
    
    /**
     * @brief 擦除块
     * @param dev 设备句柄
     * @param offset 起始偏移
     * @param size 擦除大小
     * @return esp_err_t 
     */
    esp_err_t (*erase)(vfs_block_dev_t* dev, size_t offset, size_t size);
    
    size_t block_size;      /**< 块大小 */
    size_t total_size;      /**< 总大小 */
    void* priv_data;        /**< 私有数据 */
};

/**
 * @brief 注册块设备
 */
esp_err_t vfs_block_dev_register(vfs_block_dev_t* dev);

/**
 * @brief 获取块设备
 */
vfs_block_dev_t* vfs_block_dev_get(const char* name);

#endif
```

### 2. VFS 管理器接口 (vfs_manager.h)

```c
#ifndef VFS_MANAGER_H
#define VFS_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

/** 错误码 */
typedef enum {
    VFS_OK = 0,
    VFS_ERR_NOT_INIT = -1,
    VFS_ERR_INVALID_PARAM = -2,
    VFS_ERR_MOUNT_FAILED = -3,
    VFS_ERR_DEV_NOT_FOUND = -4,
    VFS_ERR_IO = -5,
    VFS_ERR_NO_MEMORY = -6,
    VFS_ERR_ALREADY_MOUNTED = -7,
} vfs_err_t;

/** 挂载配置 */
typedef struct {
    const char* virtual_path;   /**< 虚拟路径，如 "/audio" */
    const char* device_name;    /**< 设备名称，如 "w25q128" */
    size_t partition_offset;    /**< 分区内偏移 */
    size_t partition_size;      /**< 分区大小 */
    bool format_if_fail;        /**< 挂载失败是否格式化 */
} vfs_mount_config_t;

/**
 * @brief 初始化 VFS 系统
 * @return vfs_err_t 
 */
vfs_err_t vfs_init(void);

/**
 * @brief 挂载文件系统
 * @param config 挂载配置
 * @return vfs_err_t 
 */
vfs_err_t vfs_mount(const vfs_mount_config_t* config);

/**
 * @brief 卸载文件系统
 * @param virtual_path 虚拟路径
 * @return vfs_err_t 
 */
vfs_err_t vfs_unmount(const char* virtual_path);

/**
 * @brief 打印所有挂载点状态
 */
void vfs_print_status(void);

#endif
```

---

## 四、实现步骤

### 第一步：创建 VFS 组件框架
1. 创建 `components/vfs/` 目录结构
2. 编写 `CMakeLists.txt`
3. 定义接口头文件

### 第二步：实现块设备抽象层
1. 块设备注册/查找机制
2. 内存池管理（防止碎片）

### 第三步：实现内部 Flash 块设备
1. 封装 ESP-IDF 分区 API
2. 适配块设备接口

### 第四步：实现 W25Q128 块设备
1. 封装现有 SPI 驱动
2. 适配块设备接口

### 第五步：实现 VFS 路由层
1. 路径前缀匹配
2. 调用底层文件系统

### 第六步：集成测试
1. 挂载多个存储
2. 文件读写测试
3. 性能测试

---

## 五、设备树配置 (vfs.json)

```json
{
    "vfs": {
        "block_devices": [
            {
                "name": "internal",
                "type": "esp_flash",
                "description": "ESP32内部Flash"
            },
            {
                "name": "w25q128",
                "type": "w25q128",
                "spi_bus": "spi",
                "cs_pin": 13,
                "size_mb": 16
            }
        ],
        "mounts": [
            {
                "path": "/audio",
                "device": "w25q128",
                "offset": 0,
                "size": 8388608,
                "format_if_fail": true
            },
            {
                "path": "/font",
                "device": "w25q128",
                "offset": 8388608,
                "size": 8388608,
                "format_if_fail": true
            },
            {
                "path": "/config",
                "device": "internal",
                "offset": 0,
                "size": 2097152,
                "format_if_fail": true
            }
        ]
    }
}
```

---

## 六、内存使用策略

| 场景 | 策略 | 说明 |
|------|------|------|
| 块缓冲区 | 预分配池 | 避免频繁 malloc/free，使用 PSRAM |
| LittleFS 缓存 | 固定大小 | 每个挂载点固定 4KB |
| 路径映射表 | 静态数组 | 最多支持 8 个挂载点 |

```c
// 预分配内存池（使用 PSRAM）
#define VFS_BLOCK_BUF_SIZE    4096
#define VFS_MAX_MOUNTS        8

// 使用 heap_caps_malloc(size, MALLOC_CAP_SPIRAM) 分配 PSRAM
```

---

## 七、命名规范（遵循项目规范）

- 模块 TAG：`static const char *TAG = "[VFS]";`
- 函数命名：`vfs_<module>_<action>`（如 `vfs_manager_mount`, `vfs_block_dev_register`）
- 错误码枚举：`VFS_OK`, `VFS_ERR_<reason>`
- 静态全局变量：`s_` 前缀（如 `s_mount_table`）

---

## 八、启动新对话的提示

请使用以下提示启动新对话执行 VFS 模块开发：

```
请根据 /home/olwhistle/dockerNow/esp32/programs/ovs/docs/VFS_DESIGN.md 文档，
按照 ESP32 嵌入式开发规范（esp32-embedded-dev skill），实现 VFS 虚拟文件系统模块。

硬件信息：
- ESP32-S3，16MB Flash，8MB PSRAM
- W25Q128 外部 Flash (16MB)，SPI 40MHz，引脚：SCLK=42, MISO=41, MOSI=40, CS=13
- SPI 屏幕 (ST7789) 共享 SPI 总线

已完成：
- W25Q128 驱动（components/modules/w25q128/）
- SPI 驱动（components/drivers/spi_drv/）
- 设备树（components/dtbs/）

请按文档中的实现步骤，一步步完成 VFS 模块的开发和测试。
```

---

## 九、更新日志

### 2026-09-07
- 完成 W25Q128 驱动开发，支持设备树配置
- 完成 SPI 驱动 DMA 支持
- 完成设备树 SPIFFS 自动烧录（FLASH_IN_PROJECT）
- 性能测试：W25Q128 写入 447 KB/s，读取 4357 KB/s
- 设计 VFS 虚拟文件系统方案
