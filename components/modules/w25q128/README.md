# W25Q128模块 - SPI NOR Flash存储驱动

## 概述
W25Q128模块提供对Winbond W25Q128 SPI NOR Flash（16MB）的完整驱动支持，包括初始化、读取、写入、擦除等操作。通过event_bus发布存储状态事件。

## 硬件连接
| W25Q128引脚 | ESP32-S3引脚 | 说明 |
|-------------|--------------|------|
| SLK (SCLK)  | GPIO42       | SPI时钟 |
| DO (MISO)   | GPIO41       | 主入从出 |
| DI (MOSI)   | GPIO40       | 主出从入 |
| CS           | GPIO13       | 片选 |
| VCC          | 3.3V         | 电源 |
| GND          | GND          | 地线 |

**SPI配置**：10MHz, Mode 0

## 核心功能
- ✅ JEDEC ID读取验证（0xEF 0x40 0x18）
- ✅ 扇区擦除（4KB）
- ✅ 整片擦除
- ✅ 页编程写入（256字节页对齐）
- ✅ 全双工SPI读取（保证CS持续低电平）
- ✅ 状态寄存器轮询等待
- ✅ event_bus事件发布

## API参考

### 初始化/反初始化
```c
#include "w25q128.h"

// 初始化（自动配置SPI总线并验证JEDEC ID）
w25q128_err_t ret = w25q128_init();

// 反初始化（发送掉电命令，释放SPI资源）
w25q128_deinit();
```

### 获取Flash信息
```c
w25q128_info_t info;
w25q128_get_info(&info);

printf("容量: %lu MB\n", info.total_size / (1024*1024));
printf("扇区数: %lu\n", info.sector_count);
printf("页大小: %lu bytes\n", info.page_size);
```

### 读取数据
```c
uint8_t buffer[64];
w25q128_err_t ret = w25q128_read(0x000000, buffer, sizeof(buffer));
// 使用全双工SPI传输，保证数据正确性
```

### 写入数据
```c
const char* data = "Hello W25Q128";
w25q128_err_t ret = w25q128_write(0x000000, data, strlen(data) + 1);
// 自动处理跨页写入，内部按256字节页分割
```

### 擦除操作
```c
// 擦除指定扇区（0-based索引，每扇区4KB）
w25q128_erase_sector(0);

// 整片擦除（耗时较长，约30-60秒）
w25q128_erase_chip();
```

### 状态查询
```c
if (w25q128_is_initialized()) {
    // W25Q128已初始化
}
```

## 错误码
| 错误码 | 值 | 说明 |
|--------|-----|------|
| W25Q128_OK | 0 | 成功 |
| W25Q128_ERR_SPI | -1 | SPI通信错误 |
| W25Q128_ERR_TIMEOUT | -2 | 等待超时 |
| W25Q128_ERR_PARAM | -3 | 参数错误 |
| W25Q128_ERR_NOT_INIT | -4 | 未初始化 |

## 事件总线集成
初始化成功后发布 `EVENT_STORAGE_READY` 事件：
```c
// 其他模块可订阅此事件
event_bus_subscribe(EVENT_STORAGE_READY, my_handler, NULL);
```

## 技术实现细节

### SPI读取问题与解决
**问题**：初始实现使用分离的write+read两个SPI事务，导致CS在命令和数据之间被拉高，W25Q128退出读取模式，返回0xFF。

**解决**：改用`spi_drv_transfer`全双工传输，一次性发送cmd(1)+addr(3)+dummy(N)并接收数据，保证CS在整个序列中保持低电平。

### 状态寄存器读取
使用全双工传输读取状态寄存器1：
```c
uint8_t tx[2] = {0x05, 0x00};  // READ_STATUS1 + dummy
uint8_t rx[2];
spi_drv_transfer(handle, dev, tx, rx, 2);
status = rx[1];  // 有效数据在第二个字节
```

### 超时配置
- 擦除等待：5000ms
- 写使能等待：1000ms
- 轮询间隔：1ms

## 文件结构
```
components/modules/w25q128/
├── CMakeLists.txt    # 构建配置
├── README.md         # 本文档
├── w25q128.h         # 公共API头文件
└── w25q128.c         # 驱动实现
```

## 注意事项
1. 写入前必须先擦除（NOR Flash特性：只能1→0，不能0→1）
2. 写入地址需在擦除过的区域内
3. 单次写入不能跨越256字节页边界（驱动自动处理）
4. 整片擦除耗时较长，建议在空闲时执行
5. SPI总线反初始化时会打印"not all CSses freed"警告，不影响功能

## 测试验证
- [x] JEDEC ID读取：0xEF 0x40 0x18
- [x] 扇区擦除成功
- [x] 写入数据成功
- [x] 读取数据与写入一致
- [x] Holder模块集成测试通过

## 设备树配置

W25Q128模块从设备树读取配置，不再硬编码引脚。

### 配置文件位置
`components/dtbs/config/spi.json`

### 配置示例
```json
{
    "bus": {
        "sclk_pin": 42,
        "miso_pin": 41,
        "mosi_pin": 40,
        "max_freq_mhz": 40,
        "mode": 0
    },
    "flash": {
        "compatible": "w25q128-flash",
        "cs_pin": 13,
        "spi_freq_mhz": 20,
        "size_mb": 16,
        "quad_enable": false
    }
}
```

### 配置项说明
| 配置项 | 路径 | 说明 |
|--------|------|------|
| sclk_pin | spi.bus | SPI时钟引脚 |
| miso_pin | spi.bus | 主入从出引脚 |
| mosi_pin | spi.bus | 主出从入引脚 |
| max_freq_mhz | spi.bus | 总线最大频率 |
| mode | spi.bus | SPI模式 (0-3) |
| cs_pin | spi.flash | Flash片选引脚 |
| spi_freq_mhz | spi.flash | Flash实际工作频率 |

### 使用设备树的好处
1. 修改引脚无需重新编译代码
2. 多个SPI设备共享总线配置
3. 配置集中管理，易于维护

---

## 性能参数与优化

### 当前性能 (20MHz SPI)
| 操作 | 速度 | 说明 |
|------|------|------|
| 顺序读取 | ~2.5 MB/s | 20MHz / 8 = 2.5 MB/s 理论值 |
| 页写入 | ~365 KB/s | 256字节/页，典型0.7ms/页 |
| 扇区擦除 | ~89 KB/s | 4KB/扇区，典型45ms/扇区 |
| 整片擦除 | ~30-60秒 | 16MB全片擦除 |

### W25Q128 硬件极限
| 参数 | 最大值 | 说明 |
|------|--------|------|
| SPI时钟 | 104MHz | 单线SPI最大频率 |
| Quad SPI | 80MHz | 四线SPI（需要QSPI硬件支持） |
| 页编程时间 | 0.3-3ms | 典型0.7ms |
| 扇区擦除时间 | 15-150ms | 典型45ms |

### 提升速度的方法

#### 1. 提高SPI频率（推荐）
修改 `spi.json` 中的 `spi_freq_mhz`：
```json
"flash": {
    "spi_freq_mhz": 40,  // 或 80
}
```
- ESP32-S3 SPI最高支持80MHz
- W25Q128支持最高104MHz
- 建议先测试40MHz，稳定后再提升到80MHz

#### 2. 使用DMA传输
ESP-IDF的SPI驱动默认使用轮询模式，可配置为DMA模式：
- 减少CPU占用
- 大数据块传输效率更高
- 需要修改spi_drv.c实现

#### 3. 使用Quad SPI（QSPI）
- 需要硬件支持（ESP32-S3有QSPI接口）
- 传输速度提升4倍
- 需要W25Q128启用Quad模式
- 较复杂，需要修改驱动

#### 4. 批量操作优化
```c
// 连续读取多个扇区比逐字节读取快
w25q128_read(0, buffer, 4096);  // 一次读4KB

// 连续写入整页比逐字节写入快
w25q128_write(0, data, 256);    // 一次写256字节
```

### 建议
1. **先测试40MHz**：修改设备树后测试稳定性
2. **读取密集场景**：提高频率效果最明显
3. **写入密集场景**：受限于Flash本身，提升有限
4. **擦除密集场景**：基本无法优化，是Flash硬件限制
