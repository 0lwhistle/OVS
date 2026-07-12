# Tasker — 嵌入式任务调度系统

## 概述

Tasker 是一个基于优先级的嵌入式任务调度系统，支持三种优先级级别（little / middle / lots），提供即时任务、延迟任务、周期任务的调度能力，并具备超时检测和自动级别升级功能。

## 架构

```
┌─────────────────────────────────────────────────────┐
│                    外部代码                          │
│         (main.c / web.c / 其他模块)                  │
└──────────────────────┬──────────────────────────────┘
                       │  #include "tasker.h"
                       ▼
┌─────────────────────────────────────────────────────┐
│              API/tasker  (公共接口层)                 │
│                                                      │
│  tasker_init()          — 初始化调度系统              │
│  tasker_enqueue()       — 提交任务                   │
│  tasker_cancel_*()      — 取消任务                   │
│  tasker_task_init_*()   — 创建任务节点               │
│  tasker_is_full/empty() — 查询状态                   │
└──────────────────────┬──────────────────────────────┘
                       │
          ┌────────────┼────────────┐
          ▼            ▼            ▼
┌──────────────┐ ┌──────────┐ ┌──────────┐
│ task_manager │ │task_worker│ │  logger  │
│ (队列管理)    │ │(线程调度)  │ │ (日志)    │
└──────────────┘ └──────────┘ └──────────┘
```

## 快速开始

```c
#include "tasker.h"

// 1. 任务回调函数
enum task_t my_task(void* ctx) {
    int* count = (int*)ctx;
    (*count)++;
    printf("count = %d\n", *count);
    return TASK_OK;
}

void app_main(void) {
    // 2. 初始化调度系统
    tasker_init();

    // 3. 创建并提交任务
    int* ctx = malloc(sizeof(int));
    *ctx = 0;

    struct task_node* node = tasker_task_init_li(
        0,      // period: 0 = 即时任务
        10,     // run_cnt: 执行10次
        "my_task",  // 任务名称（必须唯一）
        my_task,    // 回调函数
        ctx         // 用户上下文
    );

    tasker_enqueue(node);
}
```

## API 参考

### 初始化

```c
int tasker_init(void);
```

初始化调度系统。如果未显式调用，其他 API 在首次使用时会自动初始化。

**返回值**：`TASK_OK` 成功，否则错误码。

---

### 创建任务

```c
// 高优先级任务（默认超时 50ms）
struct task_node* tasker_task_init_li(
    int period,     // 周期(ms): 0=即时, >0=周期
    int run_cnt,    // 执行次数: -1=无限, >0=指定次数
    const char* name,   // 任务名称（唯一标识）
    task_fn fn,         // 回调函数
    void* ctx           // 用户上下文
);

// 中优先级任务（默认超时 1000ms）
struct task_node* tasker_task_init_mi(
    int period, int run_cnt,
    const char* name, task_fn fn, void* ctx
);

// 低优先级任务（自定义超时）
struct task_node* tasker_task_init_lo(
    int timeout,    // 超时时间(ms)
    int period, int run_cnt,
    const char* name, task_fn fn, void* ctx
);
```

**参数说明**：

| 参数 | 说明 |
|------|------|
| `period` | 任务周期。`0` = 即时任务（提交后立即执行），`>0` = 周期任务（每 period ms 执行一次） |
| `run_cnt` | 执行次数。`-1` = 无限循环，`>0` = 执行指定次数后自动完成 |
| `timeout` | 超时时间（仅 `tasker_task_init_lo`）。任务执行超过此时间会被标记超时 |
| `name` | 任务名称，用于取消和查找。**必须唯一** |
| `fn` | 回调函数，签名 `enum task_t (*)(void*)` |
| `ctx` | 用户上下文指针，回调时传入 |

**返回值**：`struct task_node*` 指针，失败返回 `NULL`。

---

### 提交任务

```c
int tasker_enqueue(struct task_node* node);
```

将创建好的任务提交到调度器。

**返回值**：
- `TASK_OK` — 提交成功
- `TASK_QUEUE_FULL` — 队列已满，可稍后重试
- `TASK_PARA_ERR` — 参数错误
- `TASK_INNER_ERR` — 内部错误

---

### 取消任务

```c
void tasker_cancel_by_name(const char* name);
void tasker_cancel_by_node(struct task_node* node);
```

按名称或节点指针取消任务。取消后任务将不再执行。

---

### 查询状态

```c
int tasker_is_full(void);   // 调度队列是否已满
int tasker_is_empty(void);  // 调度队列是否为空
```

---

## 优先级与级别

### 三种优先级（`enum task_priority`）

| 优先级 | 说明 |
|--------|------|
| `first` | 最高优先级，优先调度 |
| `middle` | 中等优先级 |
| `last` | 默认优先级 |

当队列满时，低优先级任务会被提升优先级以争取调度机会。

### 三种时间级别（`enum task_time_cost_level`）

| 级别 | 默认超时 | 适用场景 |
|------|---------|---------|
| `level_little` | 50ms | 短小快速的任务，如 GPIO 操作 |
| `level_middle` | 1000ms | 普通任务，如传感器读取 |
| `level_lots` | 自定义 | 耗时任务，如网络请求、文件操作 |

**级别升级**：当 little 级别任务超时时，会自动升级为 middle 级别，下次调度时获得更多执行时间。

---

## 回调函数

任务回调函数必须返回 `enum task_t`：

```c
enum task_t my_handler(void* ctx) {
    // 执行任务逻辑
    return TASK_OK;       // 成功
    // return TASK_FUNC_ERR;  // 失败（任务会被取消）
}
```

**返回值**：
- `TASK_OK` — 任务执行成功
- `TASK_FUNC_ERR` — 任务执行失败（调度器会自动取消该任务）

---

## 错误码

| 错误码 | 值 | 说明 |
|--------|----|------|
| `TASK_OK` | 0 | 成功 |
| `TASK_MEM_ERR` | -1 | 内存分配失败 |
| `TASK_TIMEOUT_ERR` | -2 | 任务超时 |
| `TASK_PARA_ERR` | -3 | 参数错误 |
| `TASK_FUNC_ERR` | -4 | 回调函数返回失败 |
| `TASK_INNER_ERR` | -5 | 内部错误 |
| `TASK_QUEUE_FULL` | -6 | 队列已满 |
| `TASK_STOP` | -7 | 调度器已停止 |

---

## 完整示例

```c
#include "tasker.h"

struct my_ctx {
    int count;
};

enum task_t periodic_task(void* ctx) {
    struct my_ctx* mc = (struct my_ctx*)ctx;
    mc->count++;
    printf("tick %d\n", mc->count);
    return TASK_OK;
}

void app_main(void) {
    tasker_init();

    // 即时任务：执行1次
    struct my_ctx* ctx1 = malloc(sizeof(struct my_ctx));
    ctx1->count = 0;
    struct task_node* n1 = tasker_task_init_li(0, 1, "oneshot", periodic_task, ctx1);
    tasker_enqueue(n1);

    // 周期任务：每200ms执行一次，无限循环
    struct my_ctx* ctx2 = malloc(sizeof(struct my_ctx));
    ctx2->count = 0;
    struct task_node* n2 = tasker_task_init_mi(200, -1, "periodic", periodic_task, ctx2);
    tasker_enqueue(n2);

    // 5秒后取消周期任务
    vTaskDelay(pdMS_TO_TICKS(5000));
    tasker_cancel_by_name("periodic");
}
```

## 注意事项

1. **任务名称必须唯一**，重复名称会导致查找/取消混乱
2. **`name` 参数**：`tasker_task_init_*` 内部会复制 name 字符串，传入的 name 可以是栈上变量
3. **`ctx` 内存管理**：由调用者负责分配和释放，调度器不会自动释放
4. **线程安全**：所有 API 都是线程安全的，可在不同任务/中断中调用
5. **FreeRTOS 环境**：内部使用 pthread 封装，在 ESP-IDF 中 pthread 基于 FreeRTOS 任务实现
