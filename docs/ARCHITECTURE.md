# Tasker 调度器架构评审与更新日志

> 记录时间：2026-07-28  
> 评审范围：modules/tasker/ (task_manager + task_worker) + API/tasker/  
> 测试配套：modules/tasker/task_worker/tasker_test.c (25 项覆盖)

---

## 一、架构概述

```
用户调用                      tasker_test_all()
    │                              │
    ▼                              ▼
┌──────────────────────────────────────────┐
│  API 层: tasker_init / tasker_enqueue    │
│  tasker_task_init_{li,mi,lo}             │
│  tasker_cancel_by_name / tasker_is_full  │
└──────────────────┬───────────────────────┘
                   │
    ┌──────────────▼────────────────────────┐
    │         Sched Table (64 slots)        │
    │  worker_sched_handler  @ ~1 tick      │
    │  - 周期注入 (period + run_cnt 决策)    │
    │  - priority counting sort 重排        │
    └──────────────┬────────────────────────┘
                   │
    ┌──────────────▼────────────────────────┐
    │        Dispatcher (64 slots)           │
    │  worker_dispatcher_handler @ ~1 tick  │
    │  - 按 priority 顺序分发               │
    │  - level → 路由到对应 worker          │
    │  - 拥塞反馈: pri_up + 重排            │
    └──┬──────────┬──────────┬─────────────┘
       │          │          │
       ▼          ▼          ▼
   ┌──────┐  ┌──────┐  ┌──────┐
   │little│  │middle│  │ lots │
   │  8槽 │  │  8槽 │  │  8槽 │
   │50ms  │  │1000ms│  │10s   │
   │ 超时 │  │ 超时 │  │ 超时 │
   └──┬───┘  └──┬───┘  └──┬───┘
      │         │         │
      │  超时 → level_up  │
      └─────────┴─────────┘
```

### 核心数据流

```
submit → sched_table(按pri排队) → dispatcher(按pri分发) 
    → worker满 → pri提升 + counting sort重排 → 下轮优先分发
    → worker执行超时 → level提升 → 迁移到更宽松的worker
```

---

## 二、技术亮点

### 1. 双轴自适应调度

| 轴 | 机制 | 触发条件 | 方向 |
|----|------|----------|------|
| 纵向 (时间) | `task_node_leve_up` | 任务执行超时 | little → middle → lots |
| 横向 (拥塞) | `task_node_pri_up` + `task_manager_pri_sort` | 目标 worker 队列满 | last → middle → first |

两轴独立运作，互不干扰。纵向解决"快 worker 不被慢任务阻塞"，横向解决"高峰期部分任务不被饿死"。

### 2. O(n) 优先级计数排序

```c
// task_manager.c — 完全在栈上操作，零堆分配
int count[4] = {0};
struct task_node* temp[size];  // VLA, max 64 × 4B ≈ 256B
```

嵌入式场景下避免 `qsort` 的递归栈风险和堆分配开销。排序只移动节点指针，不复制任务元数据。size 上限为 SCHED_TASK_QUEUE_SIZE=64，VLA 安全可控。

### 3. Move 语义 + 防御式 API

- `tasker_enqueue` 成功后设置 `node->fn = NULL`，调用方无法复用已消费的 node
- `tasker_auto_init` 允许调用方省略显式 `tasker_init`，首次调用自动初始化
- 参数校验（负周期、空名称、NULL 函数指针）在入口处统一拦截

### 4. 节点池 + 指针移动并发模型

```c
// sched_handler: 标记 dispatched → 移动指针 → 失败回滚
node->dispatched = 1;
ret = worker_task_enqueue_nocancel(s_dispatcher, node);
if (ret != TASK_OK) {
    node->dispatched = 0;
    // 回滚 inject_time / run_cnt
}
```

任务元数据在全局节点池中只存在一份，Sched → Dispatcher → Worker 之间只转移节点指针。Worker 执行完成后再清空 `dispatched`，周期任务继续留在 Sched Table。

### 5. 分区表驱动的 OTA 兼容设计

Tasker 的栈大小常量 (`LITTLE_TASK_STACK_SIZE=3072` 等) 与 ESP-IDF 配置解耦，适配 16MB Flash / 双 OTA 分区的内存约束。

---

## 三、优化改进建议

### 1. pthread / FreeRTOS 混用的语义对齐

**现状**：`vTaskDelay(1)` 和 `pthread_mutex_lock` 混用。ESP-IDF 的 pthread 是 FreeRTOS task 上的薄封装，但 `vTaskDelay(1)` 的实际粒度受 `configTICK_RATE_HZ` 影响（通常 100Hz = 10ms 一 tick）。

**建议**：
- sched_handler 和 dispatcher 的轮询周期建议改用 `esp_timer` 或 FreeRTOS `xTimer` 以获得精确周期
- 或者统一到一个并发模型：如果不需要跨核，纯 pthread 方案更可移植；如果需要 CPU pinning，纯 FreeRTOS task 方案语义更清晰

### 2. cancel 标志位的语义分离

**现状**：`cancel` 保留用户取消语义；`dispatched` 已独立表示节点正被 Dispatcher/Worker 借用，Dispatcher 不再用 `cancel` 标记源槽已消费。

**建议**：后续可进一步引入状态枚举：
```c
enum task_state { TASK_IDLE, TASK_QUEUED, TASK_DISPATCHED, TASK_CANCELLED };
```
当前指针池模型下，`cancel` + `dispatched` 已能区分“用户取消”和“执行中借用”。

### 3. worker 队列槽位的碎片化回收

**现状**：worker 的 8 槽队列通过遍历查找 `cancel || done` 的空位来复用。在高峰期槽位利用率高，但低负载时可能出现碎片（前几个槽占满、后面空着）。

**建议**：考虑用 `task_manager_pri_sort` 同时做 compact（将 active 任务前移，空槽后移），或者引入 free-list 指针。当前 8 槽规模下碎片影响不大，但如果未来扩到 32+ 槽会变得明显。

### 4. 超时定时器的 per-worker 复用

**现状**：`worker_do_handler` 里每个任务执行前后 `esp_timer_start_once` / `esp_timer_stop`，定时器是 worker 级别的单例。这意味着：
- 同一 worker 里一次只执行一个任务（for 循环串行），定时器复用是安全的
- 但如果未来改为 worker 内并发执行，会出现定时器冲突

**建议**：当前架构下无问题，但建议在 `worker_do_handler` 注释中显式标注定时器的单例假设，防止未来修改引入竞态。

### 5. counting sort 的稳定性

**现状**：`task_manager_pri_sort` 是非稳定排序——同优先级的元素相对顺序可能改变。

**建议**：对于调度表（dispatcher 按 pri 顺序分发）这不是问题，因为 dispatcher 遍历所有槽位并行的。如果未来出现"同 pri 需要 FIFO 保证"的场景，可改用稳定的 counting sort（记录每个 pri 的写入指针偏移）。

### 6. 测试覆盖盲区

**已覆盖**：25 项（15 基础 + 10 压力），包括超时、升级、取消竞速、内存搅动、CPU 饱和、耐力跑。

**未覆盖**：
- `worker_delete` 的优雅停止路径（当前仅在析构时使用）
- `enqueue_switcher` 在 `level_lots` 以外的错误路径
- 调度表在 `TASK_QUEUE_FULL` 回滚后的 `run_cnt` 精度（长周期压力下）
- 多个 worker 同时满的级联拥塞场景

---

## 四、零拷贝移动语义优化

```text
Node Pool(64) ──enqueue 移入一次──▶ Sched Table ──指针──▶ Dispatcher ──指针──▶ Worker
                                        ▲                                        │
                                        └──────────── 周期任务放回 Sched ──────────┘
```

- 内部队列存储从 `struct task_node queue[]` 改为 `struct task_node* queue[]`，每个任务节点在全局池中只存在一份。
- 新增 `TASK_NODE_POOL_SIZE = SCHED_TASK_QUEUE_SIZE` 节点池，池大小与调度表容量一致，避免 Dispatcher/Worker 再各持有一份完整任务描述。
- `tasker_enqueue` 只在入队时把调用方的 `task_node` 移动进池一次；后续 Sched → Dispatcher → Worker 只转移节点指针，不再复制完整结构体。
- 新增 `dispatched` 状态位，表示节点正被 Dispatcher/Worker 借用；取消、超时升级、周期任务继续复用同一个节点，执行结束后由 Sched 扫描回收。
- `task_manager_pri_sort` 改为指针计数排序，临时数组从约 5KB 的整结构体 VLA 降到约 256B 的指针 VLA。
- `worker_do_handler` 不再拷贝 `task_copy`，直接使用 Worker 队列中的共享节点指针执行回调。

## 五、更新日志


| 日期 | 内容 |
|------|------|
| 2026-07-28 | 架构评审文档创建。总结双轴自适应调度、counting sort、move 语义、copy-to-stack 并发模型等亮点；提出 6 项优化建议 |
| 2026-07-28 | tasker_test 重写：从 7 项压力测试扩展至 25 项（15 基础 + 10 压力），所有延迟改用 `vTaskDelay` 兼容 FreeRTOS；新增结构化的失败记录系统（测试名 + 行号 + 详情的索引输出） |
| 2026-07-28 | tasker_test.h 创建，提供 `tasker_test_all()` / `tasker_test_basic()` / `tasker_test_stress()` 三个入口供 main.c 调用 |
| 2026-07-28 | task_worker/CMakeLists.txt 更新，SRCS 加入 tasker_test.c，REQUIRES 加入 tasker |
| 2026-07-28 | task_manager.h 修复：`struct task_manager` 定义移至 `task_manager_is_empty` / `task_manager_is_full` 声明之前，消除隐式声明与定义冲突的编译错误 |
| 2026-07-28 | web.c 修复：`tasker_task_init_mi` 调用适配新签名（out 参数 + int 返回值 + 显式 `tasker_enqueue`） |
| 2026-07-31 | tasker 零拷贝移动语义优化：全局节点池 + 指针队列，入队后调度/分发/执行阶段只移动节点指针；排序临时数组改为指针 VLA |
| 2026-08-01 | Tasker 文档同步：README 架构图/容量/API 签名更新，ARCHITECTURE 补齐节点池指针移动模型 |
