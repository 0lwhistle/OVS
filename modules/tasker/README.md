# Tasker — ESP32 任务调度系统

## 目录

- [1. 架构介绍](#1-架构介绍)
- [2. API 使用文档](#2-api-使用文档)
- [3. 测试文档](#3-测试文档)
- [4. 使用指南](#4-使用指南)
  - [4.1 适用场景](#41-适用场景)
  - [4.2 使用步骤](#42-使用步骤)
  - [4.3 使用注意事项](#43-使用注意事项)
  - [4.4 最佳实践](#44-最佳实践)
  - [4.5 常见问题](#45-常见问题)

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

**调度频率：** Sched Handler 每 `vTaskDelay(1)`（FreeRTOS 1 tick，ESP32 默认 `configTICK_RATE_HZ=100`，即 10ms）扫描一次调度表。

注意：**即时任务（period=0）不受调度频率影响**——只要 `run_cnt > 0`，Sched Handler 会在下一轮扫描中立即提交到 Dispatcher，不需要等待完整的 period。只有延迟/周期任务（period>0）的精度受调度频率限制（10ms 级别）。

**建议：** 注册延迟任务和周期任务时，`period` 尽量设置为 10ms 的倍数（如 10、20、50、100、500、1000ms），避免因 tick 对齐导致实际周期与预期有偏差。例如设置 `period=15ms`，实际可能是 10ms 或 20ms 触发一次。

对于大多数 IoT 场景（传感器采集 100ms+、HTTP 请求秒级），10ms 精度完全够用。如果需要更高精度，可以在任务函数内部用 `esp_timer` 自行计时。

#### Dispatcher（分发器）
- 接收来自 Sched Table 的任务
- 根据任务的 `level`（little/middle/lots）分发给对应的 Worker
- **优先级排序**：当 Worker 队列满时（`enqueue_switcher` 返回 `TASK_QUEUE_FULL`），Dispatcher 会：
  1. 提升该任务的优先级（`task_node_pri_up`）
  2. 设置 `need_sort = 1`
  3. 遍历结束后调用 `task_manager_pri_sort` 对整个队列按优先级排序
  4. 下次遍历时高优先级任务优先被分发
- Dispatcher 每处理完一轮后 `vTaskDelay(1)` 让出 CPU

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

### 1.6 架构效率分析

#### 与常见架构对比

| 特性 | Tasker（三级调度） | 单线程轮询 | FreeRTOS 原生队列 | 事件驱动框架 |
|------|-------------------|-----------|-----------------|-------------|
| **调度精度** | 10ms（tick 级别） | 取决于轮询间隔 | 取决于 tick | 事件触发，微秒级 |
| **CPU 开销** | 低（3 个 Worker 线程 + 1 个 Handler） | 最低（单线程） | 低（无额外线程） | 中（事件循环） |
| **内存开销** | 中（~2KB 队列 + 线程栈） | 最低 | 低 | 中 |
| **任务隔离** | ✅ 三级隔离，互不影响 | ❌ 一个任务卡死全系统 | ❌ 队列满阻塞 | ✅ 事件独立处理 |
| **超时保护** | ✅ 自动检测 + 升级 | ❌ 需手动实现 | ❌ 无 | ❌ 需手动实现 |
| **优先级** | ✅ 三级优先级 + 动态排序 | ❌ 无 | ✅ FreeRTOS 原生 | ✅ 事件优先级 |
| **周期任务** | ✅ 原生支持 | ✅ 手动实现 | ❌ 需额外逻辑 | ❌ 需额外逻辑 |
| **取消机制** | ✅ 按名称/节点取消 | ❌ 需标记变量 | ❌ 需标记变量 | ✅ 可移除事件 |
| **复杂度** | 中 | 低 | 低 | 中-高 |

#### 效率数据估算

基于 ESP32-S3 @240MHz，FreeRTOS tick=10ms：

| 操作 | 耗时估算 | 说明 |
|------|---------|------|
| Sched Handler 一次扫描（32 slot） | ~5-10μs | 纯内存遍历，无锁竞争时 |
| Dispatcher 一次分发 | ~2-5μs | 复制 node + 入队 |
| Worker 取任务执行 | ~1-2μs | 出队 + 启动 timer |
| 一次完整调度周期 | ~10-20μs | 从扫描到 Worker 开始执行 |
| 调度开销占比 | < 0.1% | 假设任务执行时间 > 10ms |

#### 瓶颈分析

1. **Sched Handler 扫描**：O(n) 遍历 32 slot，n 固定为 32，时间复杂度恒定
2. **Dispatcher 分发**：O(n) 遍历 Worker 队列找空位，Worker 队列最大 8，可忽略
3. **优先级排序**：O(n log n)，仅在队列满时触发，非常见路径
4. **锁竞争**：pthread_mutex 在低冲突场景下开销 < 1μs

#### 适用场景建议

| 场景 | Tasker | 推荐方案 |
|------|--------|---------|
| 5-10 个周期任务，100ms+ 周期 | ✅ 非常适合 | Tasker |
| 大量短任务（< 1ms），高频率 | ⚠️ 调度开销占比大 | 事件驱动或裸循环 |
| 硬实时控制（PWM、电机） | ❌ 精度不够 | 硬件定时器中断 |
| 少量任务，简单逻辑 | ⚠️ 杀鸡用牛刀 | 单线程轮询 |
| 复杂任务编排，多种优先级 | ✅ 非常适合 | Tasker |

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

---

## 4. 使用指南

### 4.1 适用场景

Tasker 适合以下场景：

| 场景 | 说明 | 示例 |
|------|------|------|
| **周期性数据采集** | 定时从传感器读取数据 | 每 100ms 读一次温度传感器 |
| **异步事件处理** | 将事件处理与主逻辑解耦 | HTTP 请求处理、按键事件 |
| **后台维护任务** | 定期执行系统维护 | 内存清理、日志刷新、WiFi 重连 |
| **超时保护** | 检测任务是否卡死 | 看门狗替代方案 |
| **任务编排** | 多个任务按优先级和时间调度 | 高优先级先处理，低优先级排队 |
| **突发流量削峰** | 大量任务涌入时排队处理 | 批量数据上报、日志批量写入 |

**不适合的场景：**
- 硬实时任务（需要微秒级确定性）
- 中断上下文（Tasker 运行在线程中）
- 超短任务（执行时间 < 1ms 的任务，调度开销可能超过执行时间）

### 4.2 使用步骤

#### 第一步：选择任务级别

根据任务的预期执行时间选择级别：

| 级别 | 执行时间 | 默认超时 | 队列大小 | 适用场景 |
|------|---------|---------|---------|---------|
| `little` | < 50ms | 50ms | 4 | 传感器读取、状态更新、简单计算 |
| `middle` | < 1000ms | 1000ms | 8 | HTTP 请求、文件操作、中等计算 |
| `lots` | 自定义 | 自定义 | 8 | 大数据处理、复杂运算、批量操作 |

**经验法则：** 不确定时先用 `little`，如果超时了系统会自动升级到 `middle`。

#### 第二步：实现任务函数

```c
// 任务函数接收 void* ctx，返回 enum task_t
enum task_t my_sensor_task(void* ctx) {
    sensor_ctx_t* sctx = (sensor_ctx_t*)ctx;
    
    // 读取传感器
    float temp = sctx->read();
    
    // 处理数据（快速操作）
    sctx->buffer[sctx->index++] = temp;
    
    // 返回成功
    return TASK_OK;
}
```

**注意：** 任务函数中不要做耗时操作。如果确实需要，用 `sched_task_init_lo` 并设置合适的 timeout。

#### 第三步：创建并提交任务

```c
// 1. 创建上下文（必须 malloc，不能是栈变量）
sensor_ctx_t* ctx = malloc(sizeof(sensor_ctx_t));
ctx->read = read_temperature;

// 2. 创建任务节点
struct task_node* node = sched_task_init_li(
    100,           // 每 100ms 执行一次
    -1,            // 无限循环
    "temp_sensor", // 唯一名称
    my_sensor_task,
    ctx
);

// 3. 提交到调度表
if (shched_enqueue(node) != TASK_OK) {
    // 处理失败（调度表满）
}
```

#### 第四步：取消任务（可选）

```c
// 按名称取消
shched_cancel_by_name("temp_sensor");
```

### 4.3 使用注意事项

#### ⚠️ 任务名称必须唯一

```c
// 错误：同名任务，后提交的会覆盖前面的
sched_task_init_li(0, 1, "my_task", fn1, ctx1);
sched_task_init_li(0, 1, "my_task", fn2, ctx2);  // 覆盖！

// 正确：使用唯一名称
sched_task_init_li(0, 1, "sensor_temp", fn1, ctx1);
sched_task_init_li(0, 1, "sensor_humidity", fn2, ctx2);
```

#### ⚠️ 上下文必须 malloc，不能是栈变量

```c
// 错误：栈变量在函数返回后被销毁
void bad_example(void) {
    int ctx = 0;
    struct task_node* node = sched_task_init_li(0, 1, "bad", fn, &ctx);
    shched_enqueue(node);
}  // ctx 被销毁，任务执行时访问野指针！

// 正确：malloc 分配
void good_example(void) {
    int* ctx = malloc(sizeof(int));
    *ctx = 0;
    struct task_node* node = sched_task_init_li(0, 1, "good", fn, ctx);
    shched_enqueue(node);
}
```

#### ⚠️ 任务函数不要长时间持有锁

```c
// 错误：任务函数中 lock 一个全局锁，执行耗时操作
enum task_t bad_task(void* ctx) {
    pthread_mutex_lock(&global_lock);
    usleep(200 * 1000);  // 200ms 耗时操作
    pthread_mutex_unlock(&global_lock);
    return TASK_OK;
}

// 正确：用 lots 级别，或把耗时操作拆分成多个小任务
enum task_t good_task(void* ctx) {
    pthread_mutex_lock(&global_lock);
    // 快速操作，< 50ms
    pthread_mutex_unlock(&global_lock);
    return TASK_OK;
}
```

#### ⚠️ 注意调度表容量

调度表（Sched Table）最多 32 个任务。如果任务太多：
- `shched_enqueue` 返回 `TASK_QUEUE_FULL`
- 等待其他任务完成后再重试
- 或者取消不需要的任务释放空间

```c
// 检查调度表状态
if (shched_is_full()) {
    printf("sched table is full, waiting...");
    usleep(100 * 1000);
    ret = shched_enqueue(node);  // 重试
}
```

#### ⚠️ 周期任务的 run_cnt 语义

```c
// 执行 5 次后自动停止
sched_task_init_li(100, 5, "run_5_times", fn, ctx);

// 无限循环，直到手动取消
sched_task_init_li(100, -1, "forever", fn, ctx);

// 执行 1 次（即时任务）
sched_task_init_li(0, 1, "once", fn, ctx);
```

#### ⚠️ 超时检测是软实时的

超时检测基于 esp_timer，精度受 FreeRTOS tick 影响（通常 10ms）。如果任务执行时间刚好在超时边界附近，可能偶尔检测不到超时。

### 4.4 最佳实践

#### 1. 传感器数据采集

```c
// 每 50ms 采集一次温度，little 级别
enum task_t read_temp(void* ctx) {
    float* temp = (float*)ctx;
    *temp = read_adc(ADC_CHANNEL_TEMP);
    return TASK_OK;
}

void init_sensors(void) {
    float* temp = malloc(sizeof(float));
    struct task_node* node = sched_task_init_li(
        50, -1, "read_temp", read_temp, temp
    );
    shched_enqueue(node);
}
```

#### 2. 定时上报数据

```c
// 每 5 秒上报一次数据，middle 级别（HTTP 请求可能较慢）
enum task_t upload_data(void* ctx) {
    char* data = (char*)ctx;
    http_post("https://server.com/api", data);
    return TASK_OK;
}

void init_upload(void) {
    char* data = malloc(1024);
    snprintf(data, 1024, "sensor_data...");
    struct task_node* node = sched_task_init_mi(
        5000, -1, "upload", upload_data, data
    );
    shched_enqueue(node);
}
```

#### 3. 超时保护 + 自动恢复

```c
// little 级别任务超时后自动升级到 middle
// 如果 middle 也超时，任务会被取消
enum task_t critical_op(void* ctx) {
    // 这个操作如果超过 50ms（little 超时），
    // 系统会自动升级到 middle（超时 1000ms）
    do_something();
    return TASK_OK;
}

// 创建时用 little，超时自动升级
struct task_node* node = sched_task_init_li(
    0, 1, "critical", critical_op, ctx
);
shched_enqueue(node);
```

#### 4. 动态调整任务参数

```c
// 提交后可以直接修改 node 字段（需确保 node 指针有效）
struct task_node* node = sched_task_init_li(100, -1, "adjustable", fn, ctx);
shched_enqueue(node);

// 修改优先级
node->pri = first;  // 下次调度时优先处理

// 注意：修改 node 字段不是线程安全的
// 如果需要在其他线程修改，需要用 shched_cancel_by_name 取消后重新提交
```

### 4.5 常见问题

**Q: 任务提交后没有执行？**
A: 检查以下几点：
1. `shched_enqueue` 是否返回 `TASK_OK`
2. 任务名称是否唯一（同名任务会覆盖）
3. 调度表是否已满（`shched_is_full()`）
4. 如果是延迟任务，`period` 是否设置正确
5. 如果是有限次任务，`run_cnt` 是否 > 0

**Q: 任务执行了但结果不对？**
A: 检查上下文指针是否有效（是否用了栈变量）

**Q: 如何调试任务执行情况？**
A: 在任务函数中加 `printf` 或使用 ESP_LOG。Tasker 内部日志 tag 为 `[TASK_WORKER]`。

**Q: 调度表满了怎么办？**
A: 三种方案：
1. 等待其他任务完成，自动释放 slot
2. 取消不需要的任务：`shched_cancel_by_name("task_name")`
3. 重试提交：`shched_enqueue(node)` 返回 `TASK_QUEUE_FULL` 时等一会再试

**Q: 任务超时了会怎样？**
A: little 级别任务超时后自动升级到 middle 级别（下次在 middle worker 中执行）。middle 和 lots 级别任务超时后只记录 `is_timeout` 标志，不会自动取消。

**Q: 可以动态创建和销毁任务吗？**
A: 可以。用 `sched_task_init_li/mi/lo` 创建，`shched_enqueue` 提交，`shched_cancel_by_name` 取消。任务执行完后自动标记为 done，slot 被回收。
