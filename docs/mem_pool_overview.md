# mem_pool 内存池 —— 设计架构与技术亮点总结

| 项目 | 内容 |
|------|------|
| 位置 | `components/core/mem_pool`（核心基础件，ESP32-S3 / PC 双平台） |
| 设计文档 | `docs/superpowers/specs/2026-09-09-mem-pool-design.md` |
| 配套 | 全仓库迁移（17 文件 117 处）、`tests/test_mem.c` 41 用例、9 模块记账桶 |
| 状态 | 已上线：全业务代码接入，真机零 magic 异常运行 |

---

## 一、一句话定位

**让每一笔堆内存都有名字、有账本、有保镖**——同时保持 `malloc/free` 原签名，存量代码纯文本替换即可迁移。

---

## 二、设计架构

### 2.1 三层能力

```
┌─────────────────────────────────────────────────────────────┐
│ 第一层 堆替换层（drop-in）                                    │
│   mem_malloc / mem_calloc / mem_realloc / mem_free           │
│   与 libc 同签名同语义 → 存量代码文本替换即迁移                 │
├─────────────────────────────────────────────────────────────┤
│ 第二层 显式能力层（少量调用点，手动使用）                       │
│   mem_heap_alloc(size, caps)  ←→ heap_caps_malloc             │
│   mem_dma_alloc / mem_psram_alloc（DMA/PSRAM 归属可见）        │
├─────────────────────────────────────────────────────────────┤
│ 第三层 定长块池（热路径固定尺寸对象）                           │
│   mem_pool_create / alloc / free                              │
│   侵入式空闲栈 O(1)，零元数据开销，范围+重复归还校验             │
│   （event_bus 事件池 48×268B 建于其上，全项目唯一池实现）        │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 记账核心：8 字节分配头

```
        分配返回值
            │
            ▼
 ┌─────────────────────┬──────────────────┐
 │ mem_hdr_t (8B)      │  负载 (size B)    │
 │ magic(2) module(1)  │  用户实际使用区    │
 │ flags(1) size(4)    │                  │
 └─────────────────────┴──────────────────┘
   ▲ free 时按指针回退 8B 取头校验
```

- `magic (0x4D45)`：野指针/越界写检测；**free 前清零** → 立即捕获二次释放
- `module`：归属模块（谁分配记谁账，free 自动回账）
- 头随负载一次 malloc，负载偏移 +8，4/8 字节对齐不破坏（`_Static_assert` 锁定布局）

### 2.3 模块桶：编译期标签，零运行时开销的归属

```cmake
# 各组件 CMakeLists 一行，对组件内全部 .c 生效：
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_NET)
```

```c
/* mem.h 内联特化——类型安全，无宏污染，无调用方感知 */
static inline void* mem_malloc(size_t size) {
    return mem_alloc_((mem_module_t)MEM_MODULE_TAG, size);  /* 未定义标签 → SYS 桶 */
}
```

16 个桶（SYS/APP/NET/WEB/OTA/VFS/DTREE/LVGL/AUDIO/MEDIA/LORA/TIME/POWER/PROV/CORE/DRV，只增不删）。**归属可以后补**：先全局替换（全落 SYS），再逐组件加一行标签，两步互不阻塞。

### 2.4 防护与统计

- **magic 校验**：`free` 前清零魔数 → double-free 在第二次释放时必然命中；wild-free 读到非魔数同样拦截。默认 LOGE 拒绝（不崩），`MEM_STRICT` 编译期或 `mem_strict_set()` 运行期升级为 abort 抓现行
- **带外诊断**：拦截时打印 `ptr / 魔数原值 / size / module / 调用者返回地址`——魔数为 0 即"我们自己的块被二次释放"，配合 addr2line 一次定位到行（真机三次实战全部当日闭环）
- **统计**：每桶 `current / peak / allocs / frees / fails`，C11 atomic 计数；`mem_stat_print()` 开机打表，`mem_stat_get()` 供 `/api/mem` 与 UI 消费

### 2.5 锁策略与双平台

| 事项 | 做法 |
|---|---|
| 计数/头字段 | 短临界区（ESP=portMUX 自旋锁 / PC=pthread 互斥量），纳秒级 |
| 实际 malloc/free | **锁外原样执行**，不放大系统堆的锁竞争 |
| 平台差异 | 全部收敛在 mem.c 顶部一个 `#if defined(ESP_PLATFORM)`；接口头零平台依赖 → PC 模拟器与 ovs_tests 零改造复用 |

---

## 三、技术亮点

### 亮点 1：同签名 drop-in，迁移是"文本替换"而不是"重构"

`mem_malloc(size)` 与 `malloc(size)` 逐字节同签名同语义（含 `realloc(p,0)`、`free(NULL)` 边界），迁移仅：

```bash
sed -E 's/\bmalloc\(/mem_malloc(/g; ...'   # \b 词边界自动豁免 heap_caps_malloc/mem_free
```

实测 **117 处调用点 / 17 文件**一次性迁移，每批独立可回退；11 个组件实查零 malloc 直接豁免。迁移期间业务逻辑零改动。

### 亮点 2：归属信息"随身携带"，free 天然记对账

归属标签记在分配头里而非分配调用处 → `mem_free(ptr)` 不需要任何标签参数，账自动记回分配时的桶。分配方与释放方跨模块（生产者分配、消费者释放）时账目依然精确。

### 亮点 3：块内标志不可靠 → 带外归属判定（真机教训直接催生的设计）

事件池案例：`mem_pool_free` 会把空闲链指针写进块首，覆盖事件头内的"来源标志"字段，后续析构按块内标志路由必然出错。修正为 **`mem_pool_contains()` 地址范围判定**（纯几何计算，不受块内容改写影响）——并沉淀为 mem_pool 公共 API，任何"块内不能存元数据"的场景都适用。

### 亮点 4：防护不是摆设，是真抓过虫的

| 实战 | 什么被拦下 | 后果对比 |
|---|---|---|
| spi_drv 3 处 `heap_caps_malloc` 配 `mem_free` 混配对 | bad magic + caller 地址 → addr2line 直达 564 行 | 修复前：潜伏的偶发堆损坏，panic 现场无从查起 |
| event_bus ESP 队列悬挂栈指针（port 层语义错误） | 事件任务读死栈内存后 free 被拦截 | 修复前：随机位置崩溃 |
| 压测与负面用例 | 41 用例含 fork 验证 strict SIGABRT | 回归门禁常驻 |

### 亮点 5：账本改变排障方式

泄漏从"玄学"变成"看表"：开机 `mem_stat_print()` 直接给出各桶当前值/峰值（真机实例：LVGL 桶 25616B = 2×12800 DMA 缓冲 + 2×8 头，分毫不差；DTREE 桶 6242B = 设备树 JSON 实际大小）。内存增长类问题从"全局堆水位猜"变成"哪只桶在涨看哪"。

### 亮点 6：单实现复用，杜绝池的重复建设

定长块池是唯一池原语：event_bus 事件池、未来的音频帧池、触摸点缓冲都 `mem_pool_create` 而非各自手写 `static arr[N]`——池自带统计、耗尽兜底和防重复归还。

### 亮点 7：PC 宿主测试门禁

纯 C 无硬件依赖 → 41 用例跑在 ovs_tests（语义对表/记账精确性/负面用例 fork 验 abort/双线程 10 万次压测账目归零），10 轮连跑全绿。后续 event_bus/tasker 回归同样挂在这套门禁上。

---

## 四、成本与权衡（诚实账）

| 成本 | 量级 | 评估 |
|---|---|---|
| 8B 头/笔 | 本项目分配笔数量级下总计 <10KB | 可忽略 |
| 统计临界区 | 纳秒~微秒级，实际分配在锁外 | 无感 |
| 一次内联转发 | 编译期内联，无调用开销 | 无 |
| 不做的事（YAGNI 边界） | 无预算强制、无泄漏栈回溯、ISR 禁用、LVGL 内部不接管（预留 `LV_STDLIB_CUSTOM` 收编点） | 保持薄 |

---

## 五、数据面板（2026-09-09 真机）

- 迁移覆盖：业务代码原生 `malloc/calloc/realloc/free` 调用 **0 残留**（17 文件 117 处 + 6 处 heap_caps 统一）
- 桶分布：9 个桶标签生效（DTREE/CORE/DRV/WEB/VFS/AUDIO/APP/LVGL/SYS）
- 开机账单：总 current ≈ 26.7KB，峰值 ≈ 33KB；LVGL 25.6KB（DMA 双缓冲）、CORE 17 笔、DRV 5 笔
- 质量：真机连续运行 bad magic = 0；ovs_tests 89/89（含 mem 41）
- 固件代价：+~8KB 代码（含统计与池），ota 分区余 39-41%
