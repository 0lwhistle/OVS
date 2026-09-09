# Holder模块 - 全局硬件模块注册表管理器

## 概述
Holder模块维护一个全局注册表，用于管理所有硬件模块的注册、初始化和状态检查。它能够防止单个硬件模块故障导致整个系统崩溃。

## 设计目标
1. **模块化管理**：统一管理所有硬件模块的生命周期
2. **故障隔离**：单个模块故障不影响其他模块和系统运行
3. **状态监控**：实时监控模块状态，提供错误统计
4. **灵活配置**：支持必需模块和可选模块的区分

## 核心功能
- 模块注册/注销
- 统一初始化管理
- 状态查询和监控
- 错误计数和报告
- 线程安全操作

## 使用方法

### 1. 初始化Holder
```c
#include "holder.h"

holder_err_t ret = holder_init();
if (ret != HOLDER_OK) {
    // 处理初始化失败
}
```

### 2. 注册模块
```c
// 注册必需模块（初始化失败会阻止系统启动）
holder_register_module("st7789", st7789_init_wrapper, true, NULL);

// 注册可选模块（初始化失败只记录错误）
holder_register_module("aht30", aht30_init_wrapper, false, NULL);

// 注册带依赖的模块（总线控制器先就绪，设备模块后初始化）
static const char* const st7789_deps[] = { "spi_bus" };
holder_register_module("spi_bus", spi_bus_init_wrapper, true, NULL);
holder_register_module_ex("st7789", st7789_init_wrapper, true,
                          st7789_deps, 1, NULL);
```

### 3. 初始化所有模块
```c
// 初始化所有注册的模块
holder_ret = holder_init_all(true);  // true表示遇到必需模块错误时停止
```

使用 `holder_register_module_ex()` 声明依赖后，`holder_init_all()`
会按依赖分批初始化；缺失依赖或循环依赖的模块会被标记为 ERROR。

### 4. 查询模块状态
```c
// 检查模块是否就绪
if (holder_is_module_ready("st7789")) {
    // 模块已初始化，可以使用
}

// 获取模块状态
holder_module_state_t state = holder_get_module_state("aht30");

// 打印状态报告
holder_print_status();
```

## 错误处理机制
1. **必需模块**：初始化失败时，根据配置决定是否停止系统启动
2. **可选模块**：初始化失败时记录错误，系统继续运行
3. **错误统计**：记录每个模块的错误次数和最后错误信息
4. **状态跟踪**：实时跟踪模块状态变化

## 线程安全
- 所有公共API都是线程安全的
- 使用FreeRTOS互斥锁保护共享数据
- 支持多任务并发访问

## 可移植性设计
- 使用标准C类型（stdint.h, stdbool.h）
- 抽象平台相关代码（FreeRTOS互斥锁）
- 通过依赖注入减少耦合
- 错误码自定义，不依赖平台特定错误码

## 集成示例
参考 `holder.h` 的接口注释与 `docs/ARCHITECTURE.md` 的 Holder 章节
（注意：当前 `src/app/main.c` 尚未接入 holder，接入后各外设模块统一走注册表初始化）。

## 注意事项
1. 必须在使用任何模块功能前初始化holder
2. 模块名称必须唯一
3. 初始化函数必须返回int类型（0成功，非0失败）
4. 建议为每个硬件模块创建初始化包装函数

## 调试功能
- `holder_print_status()`：打印所有模块状态报告
- 错误计数和最后错误信息
- 初始化耗时统计
