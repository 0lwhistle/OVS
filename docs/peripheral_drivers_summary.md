# ESP32-S3 智能助手 - 外设驱动实现总结

## 项目概述

**项目路径**: `/home/olwhistle/dockerNow/esp32/programs/ovs`
**ESP-IDF版本**: v6.0.1
**编译状态**: ✅ 成功
**二进制大小**: 1,080,784 bytes (1.03 MB)
**分区大小**: 1,536,000 bytes (1.46 MB)
**剩余空间**: 30%

---

## 已实现的外设模块

### 1. ST7789 显示屏模块
- **路径**: `components/modules/st7789/`
- **接口**: SPI
- **功能**: 240x280 TFT显示屏初始化、绘图、刷新
- **事件**: `EVENT_DISPLAY_READY`

### 2. W25Q128 Flash存储模块
- **路径**: `components/modules/w25q128/`
- **接口**: SPI
- **功能**: 16MB NOR Flash读写、擦除
- **事件**: `EVENT_STORAGE_READY`
- **健康状态机**: `READY` / `FAULT`
  - 每次公开读写/擦除前先做 JEDEC ID 探测；
  - 连续 3 次失败进入 `FAULT`，之后操作立即返回
    `W25Q128_ERR_OFFLINE`；
  - `FAULT` 状态下每隔至少 1s 惰性探测，成功自动恢复 `READY`
    并发布 `EVENT_STORAGE_READY`；
  - 提供 `w25q128_health_check()` / `w25q128_is_ready()`。
- **忙等策略**: 页/扇区 5s、整片擦除 60s；轮询间隔 10ms
  （适配 FreeRTOS 100Hz，避免 `pdMS_TO_TICKS(1)=0` 空转）
- **格式化策略**: `ovs.dtb.json` 的 `vfs.mounts` 关闭 `format_if_fail`，
  仅显式调用 `vfs_format()` 才格式化，防止拔盘误清数据

### 3. LoRa 无线通信模块
- **路径**: `components/modules/lora/`
- **接口**: UART
- **功能**: LoRa数据发送/接收，周期性接收轮询
- **事件**: `EVENT_LORA_DATA_RECEIVED`, `EVENT_LORA_READY`

### 4. 音频模块
- **路径**: `components/modules/audio_module/`
- **接口**: I2S
- **功能**: 麦克风录音、功放播放
- **事件**: `EVENT_AUDIO_DATA`, `EVENT_AUDIO_READY`

### 5. ath30 温湿度传感器
- **路径**: `components/modules/ath30/`
- **接口**: I2C
- **功能**: 周期性温湿度数据采集
- **事件**: `EVENT_SENSOR_TEMP_HUMIDITY`, `EVENT_SENSOR_ERROR`

### 6. CST816S 触摸屏
- **路径**: `components/modules/cst816s/`
- **接口**: I2C
- **功能**: 触摸检测、手势识别
- **事件**: `EVENT_TOUCH_PRESS`, `EVENT_TOUCH_RELEASE`, `EVENT_TOUCH_SWIPE`

---

## 底层驱动

### SPI驱动 (`components/drivers/spi_drv/`)
- 支持设备链表，多个设备共享总线
- 从设备树读取配置
- 提供同步/异步/DMA接口

### UART驱动 (`components/drivers/uart_drv/`)
- 使用事件队列接收数据
- 支持LoRa模块通信

### I2S驱动 (`components/drivers/i2s_drv/`)
- 全双工模式，独立TX/RX通道
- 支持麦克风和功放

### I2C驱动 (`components/drivers/i2c_drv/`)
- 自动添加设备到总线
- 缓存设备句柄

### SPI总线共享分析（2026-09-08）

**当前总线拓扑**（见 `components/dtbs/config/ovs.dtb.json`，设备嵌套于 `buses` 下）：
- 总线控制器：总线节点名 `buses.spi2`（SPI2_HOST，节点名即控制器编号）
- ST7789 显示屏（2.8寸，SPI部分）：CS=GPIO48，DC=GPIO47
- W25Q128 Flash：CS=GPIO13
- 共用 SCLK=GPIO42、MOSI=GPIO40、MISO=GPIO41
- CST816S 触摸屏走 I2C，不在 SPI 总线上

**结论**：
1. 电气上可以共享：CS 独立即可安全分时访问，不会互相“打架”；
2. 已修复软件共享问题：SPI/I2C/I2S/UART 驱动已改为 Linux 式共享计数，
   同一总线只初始化一次，模块通过引用计数共享句柄；
3. 性能上是串行分时：全帧刷新约 27ms @40MHz，若 Flash 读写与刷屏抢总线，
   会造成画面卡顿；Flash 擦除等待本身不占用总线，可异步化。

**建议**：
- ✅ `spi_drv` 总线单例重构已完成，模块按“init→add_device→remove_device→
  deinit”顺序使用；
- 显示和 Flash 有并发高吞吐需求时，拆分到 `SPI2_HOST` / `SPI3_HOST`
  两条独立总线；
- 维持共享总线时，LCD 用 DMA+双缓冲，Flash 大操作放后台任务，
  静态字体文件上电后缓存到 RAM。

**总线节点命名约定**（2026-09-08 设备树重构后，host 属性已删除，
控制器编号直接写在总线节点名里）：
| 总线 | 节点名格式 | 示例 |
|------|-----------|------|
| SPI | `spi<2/3>` | `"spi2"` |
| I2C | `i2c<0/1>` | `"i2c0"` |
| I2S | `i2s<0/1>` | `"i2s0"` |
| UART | `uart<0/1/2>` | `"uart1"` |

dtree 通过 `dtree_get_host_id(bus_node, "spi"/"i2c"/"i2s"/"uart", &id)`
从总线节点名解析控制器编号；驱动按 host/port 维护多实例共享注册表。

---

## 核心库使用

### Event Bus (事件总线)
- **路径**: `components/core/event_bus/`
- **功能**: 模块间发布-订阅通信
- **使用**: 所有外设模块通过`EVENT_BUS_PUBLISH`发布事件

### Tasker (任务调度器)
- **路径**: `components/core/tasker/`
- **功能**: 周期性任务调度
- **使用**: ath30、CST816S、LoRa等模块的周期性采集

### Logger (日志系统)
- **路径**: `components/core/logger/`
- **功能**: 彩色日志输出
- **使用**: 所有模块的日志记录


### Holder (全局硬件模块注册表管理器)
- **路径**: `components/modules/holder/`
- **功能**: 管理所有硬件模块的注册、初始化和状态监控
- **特性**:
  - 全局模块注册表，统一管理硬件模块生命周期
  - 故障隔离机制，单个模块故障不影响其他模块
  - 状态跟踪和错误统计，便于调试和监控
  - 支持必需/可选模块区分，保证系统基本功能
- **使用**:
  ```c
  // 初始化holder
  holder_init();
  
  // 注册硬件模块
  holder_register_module("st7789", st7789_init_wrapper, true, NULL);
  
  // 初始化所有模块
  holder_init_all(true);
  
  // 安全使用模块
  if (holder_is_module_ready("st7789")) {
      st7789_display_data(data);
  }
  ```
- **优势**: 提高系统健壮性，防止单个硬件故障导致系统崩溃

---

## 编译问题修复

### 1. Event Bus头文件问题
**问题**: `include/event_bus.h`是占位文件，不包含实际实现
**解决**: 修改为转发到`components/core/event_bus/event_bus.h`

### 2. Logger头文件问题
**问题**: `LOGD`宏在include guard外部定义
**解决**: 修复include guard结构

### 3. W25Q128 GPIO问题
**问题**: 手动管理CS引脚导致编译错误
**解决**: 移除手动GPIO操作，由SPI驱动管理CS

### 4. CST816S编译问题
**问题**: CMakeLists.txt未包含正确的源文件和依赖
**解决**: 更新CMakeLists.txt，添加`esp_driver_gpio`依赖

### 5. 分区表问题
**问题**: 二进制文件超过1MB分区限制
**解决**: 切换到`PARTITION_TABLE_SINGLE_APP_LARGE` (2MB)

---

## 初始化流程

当前 `src/app/main.c` 中 app_main 的初始化顺序（2026-09-08 起）：

```c
void app_main(void) {
    // 1. NVS初始化
    // 2. 核心服务: event_bus_init + tasker_init
    // 3. SPIFFS 挂载（出厂设备树回退源）
    // 4. 设备树初始化 (dtree_init，A/B 槽优先、SPIFFS 回退)
    // 5. 网络栈 (net_stack_init 保护区，OVS_ENABLE_NET=1 时):
    //    ota_init → net_mgr_init → web 服务
    // 6. W25Q128 初始化 (w25q128_init)
    // 7. VFS 挂载 (vfs_stack_start，按设备树 vfs.mounts)
    // 8. 可选: OVS_RUN_APP_TESTS=1 时跑 VFS 测试与压力测试
}
```

> 注：ST7789/LoRa/音频/ath30/CST816S/心跳等外设模块的 init 当前未接入
> app_main（模块已具备，待后续开发线经 holder 或直接接入）。

---

## 事件类型汇总

| 模块 | 事件类型 | 说明 |
|------|----------|------|
| 系统 | `EVENT_SYSTEM_STARTUP` | 系统启动 |
| WiFi | `EVENT_WIFI_CONNECTED` | WiFi连接 |
| 传感器 | `EVENT_SENSOR_TEMP_HUMIDITY` | 温湿度数据 |
| 触控 | `EVENT_TOUCH_PRESS` | 触摸按下 |
| LoRa | `EVENT_LORA_DATA_RECEIVED` | LoRa数据接收 |
| 音频 | `EVENT_AUDIO_DATA` | 音频数据 |
| 存储 | `EVENT_STORAGE_READY` | 存储就绪 |
| 显示 | `EVENT_DISPLAY_READY` | 显示就绪 |

---

## 文件结构

```
components/
├── core/
│   ├── event_bus/          # 事件总线
│   ├── tasker/             # 任务调度器
│   └── logger/             # 日志系统
├── drivers/
│   ├── spi_drv/            # SPI驱动
│   ├── uart_drv/           # UART驱动
│   ├── i2s_drv/            # I2S驱动
│   └── i2c_drv/            # I2C驱动
└── modules/
    ├── st7789/             # ST7789显示屏
    ├── w25q128/            # W25Q128 Flash
    ├── lora/               # LoRa无线模块
    ├── audio_module/       # 音频模块
    ├── ath30/              # ath30传感器
    └── cst816s/            # CST816S触摸屏
```

---

## 编译命令

```bash
# 设置ESP-IDF环境
source /home/olwhistle/dockerNow/esp32/ESP-IDF/esp-idf-v6.0.1/export.sh

# 编译
idf.py build

# 烧录
idf.py -p /dev/ttyUSB0 flash

# 监控
idf.py -p /dev/ttyUSB0 monitor
```

---

## 注意事项

1. **ESP-IDF版本**: v6.0.1，API有变化
2. **编译警告**: 使用`-Werror`，所有warning都会导致编译失败
3. **格式化说明符**: ESP32-S3的`uint32_t`是`long unsigned int`，需使用`%lu`
4. **FreeRTOS头文件**: 使用`TaskHandle_t`等类型需包含`freertos/FreeRTOS.h`

---

**文档生成时间**: 2026-09-07
**编译状态**: ✅ 成功
