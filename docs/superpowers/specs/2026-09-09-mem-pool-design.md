# mem_pool 核心内存管理模块 设计文档

| 项目 | 内容 |
|------|------|
| 版本 | v1.0（待评审） |
| 日期 | 2026-09-09 |
| 位置 | `components/core/mem_pool`（核心基础件，双平台编译） |
| 关联 | REFACTORING_PLAN v1.1 附录 D #8；5.1 event_bus 池建于其上 |

---

## 1. 目标与非目标

**目标**：
1. 提供与 `malloc/free/calloc/realloc` **逐字节同签名**的替换接口，存量代码可纯文本替换迁移；
2. 按模块记账内存（当前值/峰值/次数），泄漏从"玄学"变"看表"；
3. 检测野指针/重复释放（magic 校验）；
4. 提供定长块池，event_bus 5.1 的池、音频帧池等热路径统一建于其上；
5. 双平台：PC 模拟器零改造编译复用（沿用 logger 模式）。

**非目标（v1 明确不做）**：
- 不做每模块预算强制（只统计+打印，预算告警 v2 视需要加）；
- 不做泄漏调用栈回溯（平台绑定重，需要时用 IDF heap tracing）；
- 不迁移 LVGL 内部分配（走自身 CLIB malloc，后期经 `LV_STDLIB_CUSTOM` 收编）；
- **禁止 ISR 上下文调用任何 mem 接口**（写进头文件注释铁律）。

## 2. 方案选型

| 方案 | 思路 | 否决/采纳理由 |
|------|------|--------------|
| A. 纯 wrapper（mem_malloc 内部直接调 malloc，不记账） | 最薄，迁移零风险 | 无归属无防护，价值仅剩"以后好改"；**否决** |
| B. **分配头 + 模块标签（本方案）** | 每笔分配垫 8B 头记录归属/大小/magic；模块经编译定义绑定标签 | 记账+防护双得，开销 8B/笔可接受；**采纳** |
| C. IDF heap_caps 追踪 / heap tracing | 平台原生能力 | 平台绑定死（PC 不可用）、无模块语义（只有任务/地址维度）；**否决** |

## 3. API 设计

### 3.1 第一层：堆替换层（drop-in，文本替换目标）

```c
/* mem.h —— 与 libc 同签名同语义 */
void* mem_malloc (size_t size);
void* mem_calloc (size_t n, size_t size);      /* 含 n*size 溢出检查 */
void* mem_realloc(void* ptr, size_t size);     /* 含 realloc(NULL,s)=malloc、realloc(p,0)=free+NULL */
void  mem_free   (void* ptr);                  /* free(NULL) 无害；magic 校验；幂等 */
```

语义保证表：

| 输入 | 行为 | 与 libc 一致性 |
|------|------|----------------|
| `mem_malloc(0)` | 返回 NULL 或唯一指针（由底层 malloc 决定），可 free | 一致 |
| `mem_free(NULL)` | 无操作 | 一致 |
| `mem_realloc(NULL, s)` | 等价 mem_malloc(s) | 一致 |
| `mem_realloc(p, 0)` | 释放 p，返回 NULL | 一致（newlib 语义） |
| `mem_realloc(p, s)` | 新块拷贝 min(旧,新)，释放旧块 | 一致（峰值统计可能瞬时双计，接受） |
| magic 不符的 free/realloc | LOGE 拒绝；MEM_STRICT=1 时 abort | 扩展防护 |

### 3.2 第二层：显式能力层（少量调用点，手动迁移，不做文本替换）

```c
void* mem_heap_alloc (size_t size, uint32_t caps); /* ↔ heap_caps_malloc（display_port/w25q128/audio） */
void* mem_dma_alloc  (size_t size);                /* = mem_heap_alloc(size, MALLOC_CAP_DMA) */
void* mem_psram_alloc(size_t size);                /* PC 上退化为 malloc */

/* 定长块池 */
mem_pool_t* mem_pool_create(const char* name, size_t block_size, size_t blocks,
                            mem_module_t owner);
void* mem_pool_alloc(mem_pool_t* pool);            /* O(1) LIFO，耗尽返回 NULL */
void  mem_pool_free (mem_pool_t* pool, void* blk); /* 范围+对齐校验，误插队报 LOGE */
```

- 块池用**侵入式单链空闲栈**，块本身零元数据开销；池元数据 init 期经 mem_heap_alloc 分配；
- 块池指针**必须**走 `mem_pool_free`；误传给 `mem_free` 会被 magic 检查拦下报错；
- event_bus 5.1 的 `static event_t pool[48]` 改为 `mem_pool_create("event", sizeof(event_t), 48, MEM_MOD_SYS)`，全工程唯一池实现。

### 3.3 统计与诊断

```c
void mem_stat_print(void);            /* 表格输出到 logger */
const mem_mod_stat_t* mem_stat_get(mem_module_t mod);   /* 后期 /api/mem、UI 关于页数据源 */
void mem_strict_set(bool on);         /* 运行期开/关 abort 模式（测试用） */
```

`mem_stat_print()` 输出示例：

```
[MEM] module      current    peak   allocs   frees   fails
[MEM] SYS            1234    5678      120     119       0
[MEM] VFS           24576   32768       54      50       0
[MEM] NET            8192   12288      300     299       1
[MEM] total        128765  200000     1900    1800       1
```

## 4. 关键机制

### 4.1 分配头（8B，防野指针 + 记账载体）

```c
typedef struct {
    uint16_t magic;    /* 0x4D45 'ME' */
    uint8_t  module;   /* MEM_MOD_* */
    uint8_t  flags;    /* heap/dma/psram */
    uint32_t size;     /* 请求字节数 */
} mem_hdr_t;           /* sizeof=8，负载偏移 +8 → 4/8 字节对齐不破坏 */
```

- 头随负载一起 malloc（`malloc(size + sizeof(mem_hdr_t))`），free 时按指针回退 8B 校验；
- 16 字节对齐等特殊需求（本项目暂无）走 `mem_heap_alloc` 显式路径，文档注明；
- 8B/笔开销：本项目最大单模块分配量级 <10^3 笔，总开销 <8KB，可忽略。

### 4.2 模块标签绑定（推荐机制：CMake 编译定义）

```cmake
# 各组件 CMakeLists.txt 一行，对本组件全部 .c 自动生效，零头文件纪律：
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_NET)
```

```c
/* mem.h 对应机制：类型安全内联转发（未定义标签的 TU 落 MEM_MOD_SYS 桶） */
#ifdef MEM_MODULE_TAG
static inline void* mem_malloc(size_t size) { return mem_alloc_(MEM_MODULE_TAG, size); }
#else
static inline void* mem_malloc(size_t size) { return mem_alloc_(MEM_MOD_SYS, size); }
#endif
/* calloc/realloc 同构；free 无需标签——从头里读归属 */
```

- 归属可以**后补**：先全局替换（全落 SYS 桶，功能无损），再逐组件加一行 CMake 定义（归属逐渐清晰），两步互不阻塞；
- 备选机制（头文件顶部 `#define MEM_MODULE_TAG` + include mem.h）同样支持，作为同组件内细分的例外手段。

### 4.3 模块枚举（初版 14 个）

```c
typedef enum {
    MEM_MOD_SYS = 0,   /* 未标注/核心杂项 */
    MEM_MOD_APP,       /* main/app 编排层 */
    MEM_MOD_NET,       /* net_mgr + wifi 驱动 */
    MEM_MOD_WEB,       /* web + mongoose */
    MEM_MOD_OTA,
    MEM_MOD_VFS,       /* ovs_vfs + w25q128 + internal_flash */
    MEM_MOD_DTREE,
    MEM_MOD_LVGL,      /* 预留：LVGL 分配器收编后启用 */
    MEM_MOD_AUDIO,
    MEM_MOD_MEDIA,
    MEM_MOD_LORA,
    MEM_MOD_TIME,
    MEM_MOD_POWER,
    MEM_MOD_PROV,      /* ble_prov */
    MEM_MOD_COUNT
} mem_module_t;
```

### 4.4 线程安全与锁策略

- 统计更新在**短临界区**（ESP: `portENTER_CRITICAL` 自旋锁；PC: pthread_mutex），纳秒~微秒级；
- 实际 malloc/free 在锁外原样执行，锁内只碰计数器和头字段；
- 顺序：先分配后记账（分配失败不进账），free 先销账后释放；
- PC 端（`OVS_SIMULATOR`）临界区宏退化为 pthread 或空操作，接口零差异。

## 5. 迁移计划（机械、可分批、可回退）

```bash
# 词边界替换：heap_caps_malloc / mem_free / SPIFFS_free 等因下划线无词边界自动豁免
sed -E 's/\bmalloc\(/mem_malloc(/g; s/\bcalloc\(/mem_calloc(/g;
        s/\brealloc\(/mem_realloc(/g; s/\bfree\(/mem_free(/g' -i <模块>/*.c
```

| 批次 | 范围 | 回归手段 |
|------|------|----------|
| 1 | ovs_vfs + dtbs | VFS 压测 54/54 + mem_stat 表核对 |
| 2 | ota + net_mgr + web | OTA 推送全流程 + WS 推送 |
| 3 | event_bus / tasker / 其余 core | ovs_tests（PC） |
| 4 | 剩余 modules + src/app | 全量真机冒烟 |

- cJSON 特殊照顾：`cJSON_Init()` 挂 mem 钩子，JSON 内存白得记账（记 MEM_MOD_SYS 或专用桶）；
- 每批独立提交，任何一批异常可单独 revert；
- 替换后 diff 人工扫一遍注释/字符串（词边界机制理论上不误伤）。

## 6. 测试计划

**PC 宿主（ovs_tests，新建，复用 sim CMake 工具链）**：
1. malloc/calloc/realloc/free 语义逐条对表（§3.1 全部行）；
2. double-free / 野 free：STRICT 关 → LOGE 不崩；STRICT 开 → abort；
3. 记账核对：分配 N 字节 → SYS 桶 current 精确 +N+8（头开销入账）、free 精确回落；
4. 峰值：嵌套分配/释放序列峰值正确；realloc 瞬时双计行为 documented；
5. 块池：耗尽返回 NULL、LIFO 复用、外培养（越界指针）被范围校验拦截；
6. 压测：双线程（PC pthread）各 10 万次分配/释放随机尺寸，结束时 total current 归零、无 magic 错。

**真机**：
1. 双平台构建门禁；
2. 批次 1 迁移后 VFS 压测 54/54；
3. 开机 `mem_stat_print()` 表格数字合理（对比迁移前 heap 水位）；
4. OTA 全流程回归（迁移涉及 ota/web 后）。

## 7. 工作量与验收

- 组件实现 ~350 行 + PC 测试 ~250 行 + CMake：**1 个会话内**完成组件+测试+批次 1 迁移；
- 验收门禁：双平台构建过 / ovs_tests 全绿 / VFS 压测 54/54 / mem_stat 数字与 heap 水位交叉吻合。

## 8. 待决策点（已附推荐，可否决）

| # | 决策 | 推荐 |
|---|------|------|
| 1 | magic 错误默认行为 | 默认 LOGE 继续（不崩）；MEM_STRICT 编译定义仅给 ovs_tests 和 debug 构建；`mem_strict_set()` 运行期可切 |
| 2 | 8B/笔头开销 | 接受（本项目分配笔数量级下总开销 <8KB） |
| 3 | 模块枚举初版 | 14 个（含 APP，见 §4.3），后期加不改 ABI（枚举只增不删） |
