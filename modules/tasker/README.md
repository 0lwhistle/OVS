# Tasker — ESP32 任务调度系统

## 目录

- [1. 架构介绍](#1-架构介绍)
- [2. API 使用文档](#2-api-使用文档)
- [3. 测试文档](#3-测试文档)

---

## 1. 架构介绍

### 1.1 概述

Tasker 是一个基于 pthread 的多级任务调度系统，运行在 ESP32 的 FreeRTOS 之上。它将任务按时间成本和优先级分为三个级别，通过调度表（Sched Table）、分发器（Dispatcher）和三级 Worker 流水线处理任务。

### 1.2 整体架构

```
                    ┌─────────────────────────────────┐
                    │          Sched Table             │
                    │   (调度表，存储待调度任务)          │
                    │   queue_size = 32                │
                    └────────────┬────────────────────┘
                                 │
                    Sched Handler (定时扫描)
                                 │
                    ┌────────────▼────────────────────┐
                    │          Dispatcher              │
                    │   (分发器，按级别分发任务)          │
                    │   queue_size = 32                │
                    └────┬──────────┬──────────┬───────┘
                         │          │          │
                    ┌────▼───┐ ┌───▼────┐ ┌───▼────┐
                    │ Little  │ │ Middle │ │  Lots  │
                    │ Worker  │ │ Worker │ │ Worker │
                    │queue=4  │ │queue=8 │ │queue=8 │
                    │timeout  │ │timeout │ │timeout │
                    │  =50ms  │ │ =1000ms│ │=10000ms│
                    └─────────┘ └────────┘ └────────┘
```

### 1.3 核心组件

#### Sched Table（调度表）
- 存储所有待调度的任务
- 支持即时任务（period=0）、延迟任务（period>0）、周期任务（run_cnt=-1）
- Sched Handler 线程定时扫描，将到期的任务提交给 Dispatcher

#### Dispatcher（分发器）
- 接收来自 Sched Table 的任务
- 根据任务的 `level`（little/middle/lots）分发给对应的 Worker
- 支持优先级排序（`task_manager_pri_sort`）

#### Worker（三级工作者）
- **Little Worker**：处理短任务（默认超时 50ms），队列大小 4
- **Middle Worker**：处理中等任务（默认超时 1000ms），队列大小 8
- **Lots Worker**：处理长任务（默认超时 10000ms），队列大小 8

### 1.4 任务生命周期

```
创建任务 → 提交到 Sched Table → Sched Handler 扫描
  → 到期 → 提交到 Dispatcher → Dispatcher 分发
  → Worker 执行 → 完成/超时/失败
```

### 1.5 关键特性

- **三级时间成本**：little / middle / lots，自动路由到对应 Worker
- **三级优先级**：first / middle / last，Dispatcher 支持优先级排序
- **超时检测**：每个 Worker 有一个复用的 esp_timer，执行任务前启动，执行后停止
- **超时升级**：little 级别任务超时后自动升级到 middle 级别
- **即时/延迟/周期**：支持三种调度模式
- **取消机制**：支持按名称取消任务

---

## 2. API 使用文档

### 2.1 头文件

```c
#include "task_manager.h"
#include "task_worker.h"
```

### 2.2 任务初始化 API

#### `sched_task_init_li` — 创建 little 级别任务

```c
struct task_node* sched_task_init_li(
    const int period,      // 调度周期(ms)，0=即时，>0=延迟/周期
    const int run_cnt,     // 执行次数，>0=有限次，-1=无限循环
    const char* name,      // 任务名称（必须唯一）
    task_fn fn,            // 任务函数
    void* ctx              // 任务上下文参数
);
```

- 默认超时：50ms
- 默认优先级：last

#### `sched_task_init_mi` — 创建 middle 级别任务

```c
struct task_node* sched_task_init_mi(
    const int period,      // 调度周期(ms)
    const int run_cnt,     // 执行次数
    const char* name,      // 任务名称
    task_fn fn,            // 任务函数
    void* ctx              // 任务上下文参数
);
```

- 默认超时：1000ms
- 默认优先级：last

#### `sched_task_init_lo` — 创建 lots 级别任务

```c
struct task_node* sched_task_init_lo(
    const int timeout,     // 自定义超时(ms)
    const int period,      // 调度周期(ms)
    const int run_cnt,     // 执行次数
    const char* name,      // 任务名称
    task_fn fn,            // 任务函数
    void* ctx              // 任务上下文参数
);
```

- 默认优先级：last

### 2.3 任务函数签名

```c
enum task_t (*task_fn)(void* ctx);
```

返回值：
- `TASK_OK` = 0：任务执行成功
- `TASK_FUNC_ERR` = -4：任务执行失败（会被自动取消）

### 2.4 调度 API

#### `shched_enqueue` — 提交任务到调度表

```c
int shched_enqueue(struct task_node* node);
```

返回值：
- `TASK_OK`：提交成功
- `TASK_QUEUE_FULL`：调度表已满
- `TASK_PARA_ERR`：参数错误

#### `shched_cancel_by_name` — 按名称取消任务

```c
void shched_cancel_by_name(const char* name);
```

#### `shched_cancel_by_node` — 按节点取消任务

```c
void shched_cancel_by_node(struct task_node* node);
```

#### `shched_is_full` / `shched_is_empty` — 检查调度表状态

```c
int shched_is_full(void);
int shched_is_empty(void);
```

### 2.5 任务节点字段

```c
struct task_node {
    int done;              // 是否完成
    int cancel;            // 是否取消
    int timeout;           // 超时时间(ms)
    int is_timeout;        // 是否超时
    int period;            // 调度周期(ms)
    int run_cnt;           // 剩余执行次数
    enum task_priority pri;    // 优先级 first/middle/last
    enum task_time_cost_level level;  // 级别 little/middle/lots
    task_fn fn;            // 任务函数
    uint64_t inject_time;  // 注入时间戳
    char* name;            // 任务名称
    void* ctx;             // 上下文参数
};
```

### 2.6 完整示例

```c
#include "task_manager.h"
#include "task_worker.h"

// 任务函数
enum task_t my_task(void* ctx) {
    int* count = (int*)ctx;
    (*count)++;
    printf("my_task executed %d times\n", *count);
    return TASK_OK;
}

void app_main(void) {
    // 1. 创建上下文
    int* ctx = malloc(sizeof(int));
    *ctx = 0;

    // 2. 创建即时任务（little 级别，执行 5 次）
    struct task_node* node = sched_task_init_li(
        0,           // period=0 表示即时任务
        5,           // 执行 5 次
        "my_task",   // 任务名称
        my_task,     // 任务函数
        ctx          // 上下文
    );

    // 3. 提交到调度表
    int ret = shched_enqueue(node);
    if (ret != TASK_OK) {
        printf("enqueue failed: %d\n", ret);
    }

    // 4. 创建周期任务（middle 级别，每 1000ms 执行一次，无限循环）
    struct task_node* periodic = sched_task_init_mi(
        1000,        // period=1000ms
        -1,          // run_cnt=-1 表示无限循环
        "my_periodic",
        my_task,
        ctx
    );
    shched_enqueue(periodic);

    // 5. 取消任务
    // shched_cancel_by_name("my_task");
}
```

### 2.7 调度模式速查

| period | run_cnt | 行为 |
|--------|---------|------|
| 0 | >0 | 即时任务，立即执行 N 次 |
| >0 | >0 | 延迟任务，每 period ms 执行一次，共 N 次 |
| >0 | -1 | 周期任务，每 period ms 执行一次，无限循环 |
| 0 | -1 | 无效（不会调度） |

---

## 3. 测试文档

### 3.1 测试环境

- 硬件：ESP32-S3
- 框架：ESP-IDF v6.0.1
- 测试文件：`main/main.c`

### 3.2 测试用例（22 个）

#### 基础功能测试（1-15）

| # | 名称 | 描述 | 验证点 |
|---|------|------|--------|
| 1 | Null Parameter | `shched_enqueue(NULL)` | 返回 `TASK_PARA_ERR`，不崩溃 |
| 2 | Cancel Non-existent | 取消不存在的任务 | 不崩溃，打印警告 |
| 3 | Check Status | 检查初始状态 | `is_full=0, is_empty=1` |
| 4 | Basic Immediate | little 级别即时任务执行 10 次 | 任务正确执行 10 次 |
| 5 | Task Fail | 返回 `TASK_FUNC_ERR` 的任务 | 任务被自动取消 |
| 6 | Cancel Task | 提交后立即取消 | 任务不执行 |
| 7 | Multi Task | 5 个任务同时提交 | 全部正确执行 |
| 8 | Priority Mix | first + last 优先级混合 | 高优先级先执行 |
| 9 | Delayed Schedule | period=100ms，执行 5 次 | 按周期正确执行 |
| 10 | Timeout Detection | little 任务 sleep 200ms（超时 50ms） | 检测到超时 |
| 11 | Level Upgrade | 超时后 little→middle 升级 | 级别正确升级 |
| 12 | Lots Level | lots 级别慢任务执行 3 次 | 正确路由到 lots worker |
| 13 | Periodic Task | 无限周期任务（500ms） | 持续执行 |
| 14 | Cancel Periodic | 1 秒后取消周期任务 | 任务停止 |
| 15 | Sched Full | 提交 40 个任务（队列 32） | 队列满时返回 `TASK_QUEUE_FULL` |

#### 压力测试（16-22）

| # | 名称 | 描述 | 验证点 |
|---|------|------|--------|
| 16 | Bulk Immediate | 50 个即时任务批量提交 | 全部入队，不崩溃 |
| 17 | Mixed Levels | 10 little + 10 middle + 10 lots | 正确路由到对应 worker |
| 18 | Burst Submit | 3 波 x 20 个任务，间隔 100ms | 突发流量下稳定 |
| 19 | Periodic Storm | 10 个不同周期的周期任务 | 多周期任务共存 |
| 20 | Cancel Storm | 20 个任务，一半立即取消 | 取消不影响其他任务 |
| 21 | Timeout Storm | 15 个超时任务（sleep 200ms） | 超时检测正常 |
| 22 | Sched Full Retry | 50 个任务，满时等待重试 | 重试机制正常 |

### 3.3 测试结果

```
========================================
  TEST SUMMARY
  Total: 22, Passed: 22, Failed: 0
========================================
```

### 3.4 已知限制

- 任务名称必须唯一（`find_task_node_by_name` 返回第一个匹配）
- 调度表满时任务优先级会被提升（`task_node_pri_up`），下次更容易被调度
- 超时检测基于 esp_timer，精度受系统 tick 影响
- 所有 Worker 线程在 FreeRTOS 中作为 pthread 运行，栈大小可配置
