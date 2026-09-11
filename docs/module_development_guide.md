# OVS 模块开发指导文档

| 项目 | 内容 |
|------|------|
| 版本 | v1.0（2026-09-09） |
| 阅读对象 | 在本工程上开发新模块/新驱动的开发者（人/AI 均适用） |
| 配套文档 | `docs/REFACTORING_PLAN.md`（架构总纲）、`docs/ARCHITECTURE.md`（现行架构）、`docs/development_log.md` |

> 本文档所有 API 签名均与当前代码逐一核对（2026-09-09 基线）。改动核心模块后请同步修订本文。

---

## 一、60 秒总览

```
应用层   src/app（main.c 瘦编排 + app_init.c 注册表）  src/lvgl（UI 六层）
服务层   components/modules/   ← 你的新模块绝大多数放这里
核心层   components/core/      logger / mem_pool / event_bus / tasker / ovs_vfs
驱动层   components/drivers/   spi_drv / i2c_drv / i2s_drv / uart_drv / wifi
配置层   components/dtbs/      设备树 ovs.dtb.json（唯一硬件权威源）+ dtree 解析器
第三方   thirdparty/           lvgl / mongoose / cJSON / sha256（勿改）
```

**开发新模块的七个步骤**（下文逐项展开）：

1. 建目录 `components/modules/<name>/`，写 `my_module.h` / `my_module.c` / `CMakeLists.txt`
2. 设备树加节点（如需要硬件参数），代码里用 `compatible` 定位读取
3. 根 `CMakeLists.txt` 的 `EXTRA_COMPONENT_DIRS` 注册组件
4. 依赖方 CMake 加 `REQUIRES <name>`（谁用谁加）
5. `src/app/app_init.c` 注册进 holder（选 required / optional）
6. 需要对外通知 → 在 `event_bus_types.h` 加事件；需要网页可见 → `web.c` 加路由/订阅
7. `idf.py build` + OTA 推送验证，写开发日志

---

## 二、新建模块的文件与目录结构

以一个名为 `speaker` 的模块为例：

```
components/modules/speaker/
├── speaker.h          # 公共 API（对外唯一接口）
├── speaker.c          # 实现
└── CMakeLists.txt     # 组件构建声明
```

### 2.1 speaker.h 模板

```c
/**
 * @file speaker.h
 * @brief 扬声器（MAX98357A I2S 功放）驱动/服务
 */

#ifndef SPEAKER_H
#define SPEAKER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 错误码：模块前缀 + 大写，0 为成功，负数为错误 */
typedef enum {
    SPEAKER_OK          = 0,
    SPEAKER_ERR_I2C     = -1,   /* 按你的模块换成实际错误类别 */
    SPEAKER_ERR_PARAM   = -2,
    SPEAKER_ERR_STATE   = -3,   /* 未初始化/重复初始化等状态错误 */
    SPEAKER_ERR_DMA     = -4,
} speaker_err_t;

/* ---- 公共 API：命名规范 module_action()，首个参数通常是句柄或无 ---- */
speaker_err_t speaker_init(void);
speaker_err_t speaker_deinit(void);
bool speaker_is_initialized(void);

/* 音量 0-100；播放 PCM 数据（16bit/16kHz/mono 由设备树采样率决定） */
speaker_err_t speaker_write_pcm(const void* data, size_t size, size_t* bytes_written);
speaker_err_t speaker_volume_set(uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif /* SPEAKER_H */
```

**头文件规范**：
- guard 命名 `模块名_H`；`extern "C"` 包裹（C++ 兼容）
- 对外只暴露 API 和错误码枚举；内部结构、静态变量一律不进头文件
- 每个函数写 doxygen 注释（`@return` 必须列全错误码）

### 2.2 speaker.c 骨架（设备树绑定 + 总线驱动标准模式）

```c
#include "speaker.h"
#include "i2s_drv.h"
#include "dtree.h"
#include "logger.h"
#include "mem.h"

static const char* TAG = "[SPEAKER]";

#define SPEAKER_DT_COMPAT  "i2s-amplifier"   /* 设备树 compatible，见 ovs.dtb.json */

static bool s_initialized = false;
static i2s_drv_handle_t s_i2s = NULL;

speaker_err_t speaker_init(void) {
    if (s_initialized) {
        LOGW(TAG, "Already initialized");
        return SPEAKER_OK;                       /* 幂等：重复调用直接成功 */
    }
    LOGI(TAG, "Initializing speaker...");

    /* 1. 按 compatible 定位设备节点（配置与代码分离的核心纪律） */
    dtree_node_t* dev = dtree_find_by_compatible(SPEAKER_DT_COMPAT);
    if (!dev) {
        LOGE(TAG, "Device node '%s' not found", SPEAKER_DT_COMPAT);
        return SPEAKER_ERR_PARAM;
    }

    /* 2. 父节点即所属总线，从总线节点加载配置（引用计数复用） */
    dtree_node_t* bus = dtree_get_parent(dev);
    i2s_drv_config_t cfg;
    if (i2s_drv_load_config(bus, &cfg) != I2S_DRV_OK) {
        return SPEAKER_ERR_DMA;
    }
    if (i2s_drv_init(&cfg, &s_i2s) != I2S_DRV_OK) {
        return SPEAKER_ERR_DMA;
    }

    s_initialized = true;
    LOGI(TAG, "Speaker initialized");
    return SPEAKER_OK;
}

speaker_err_t speaker_write_pcm(const void* data, size_t size, size_t* written) {
    if (!s_initialized) return SPEAKER_ERR_STATE;
    if (!data || size == 0) return SPEAKER_ERR_PARAM;
    return (i2s_drv_write(s_i2s, data, size, written) == I2S_DRV_OK)
           ? SPEAKER_OK : SPEAKER_ERR_DMA;
}

bool speaker_is_initialized(void) { return s_initialized; }
```

**实现规范**：
- `init` 必须幂等（重复调用返回 OK）；`xxx_is_initialized()` 必须提供
- 所有错误路径 `LOGE` 后返回错误码，**不要**在驱动里 `abort`/死等
- 硬件参数（引脚/频率/地址）只从设备树读，**禁止硬编码**（st7789 的 240×280 教训）
- 静态变量命名 `s_xxx`，全局 `g_xxx`，类型 `模块名_xxx_t`

### 2.3 CMakeLists.txt 模板

```cmake
idf_component_register(
    SRCS "speaker.c"
    INCLUDE_DIRS "."
    # 依赖原则：只 REQUIRES 你实际 include 了其公共头的组件
    REQUIRES i2s_drv dtbs mem_pool logger event_bus
)
# mem_pool 模块标签：本组件所有 mem_malloc 计入 SPEAKER 桶
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEM_MODULE_TAG=MEM_MOD_AUDIO)
```

### 2.4 接入构建系统（两处）

```cmake
# ① 根 CMakeLists.txt 的 EXTRA_COMPONENT_DIRS 追加：
    ${CMAKE_SOURCE_DIR}/components/modules/speaker

# ② 依赖方（通常是 main/CMakeLists.txt）REQUIRES 列表追加 speaker
```

### 2.5 holder 注册（src/app/app_init.c）

```c
#include "speaker.h"

static int mod_speaker(void) {
    if (force_fail_check("speaker")) return -1;          /* 降级验收钩子 */
    return (speaker_init() == SPEAKER_OK) ? 0 : -1;
}

// app_init_setup() 中追加（依赖 dtree；optional=失败只降级不阻断启动）：
static const char* const DEPS_DTREE[] = {"event_bus", "tasker", "dtree", NULL};  // 已有
holder_register_module_ex("speaker", mod_speaker, /*required=*/false,
                          DEPS_DTREE, 1, NULL);
```

**required 还是 optional？**
- 失败后**系统无法安全运行**的 → required（如存储、网络栈）。required 失败 = holder 停止启动 → OTA 15s 回滚兜底
- 失败后**功能缺失但系统可用**的 → optional（屏幕/触摸/传感器/扬声器/lora 全部是 optional）

完成后 `/api/modules` 和开机串口表会自动展示你的模块状态与 init 耗时——**不需要**再写任何状态上报代码。

---

## 三、核心模块 API 指导

### 3.1 logger —— 日志（唯一日志入口，直接用，无初始化）

```c
#include "logger.h"
static const char* TAG = "[SPEAKER]";

LOGE(TAG, "send failed: %d", err);   /* 红，错误 */
LOGW(TAG, "retry %d/%d", n, max);    /* 黄，警告 */
LOGI(TAG, "speaker initialized");    /* 绿，关键流程（默认等级） */
LOGD(TAG, "frame %zu bytes", n);     /* 灰，调试 */

logger_set_level(LOG_LEVEL_WARN);    /* 运行期调级（越界自动钳位） */
```

注意：
- 等级过滤：`ERROR` 只出 LOGE；`WARNING` 出 LOGE/LOGW；`INFO`（默认）全出
- **线程安全**（printf 级），但不要在 ISR 里用；一行日志一个 LOGx，不要拼长句
- 本模块是纯 printf 实现、PC 模拟器零改造复用——**不要**把它改成 esp_log

### 3.2 mem_pool —— 内存（业务代码禁用裸 malloc/free）

```c
#include "mem.h"

/* 日常分配：与 malloc/free 同签名，已按组件 MEM_MODULE_TAG 记账 */
void* buf = mem_malloc(256);
buf = mem_realloc(buf, 512);
mem_free(buf);                       /* free(NULL) 安全；重复/野 free 会被拦截并 LOGE */

/* 指定内存能力（显式场景才用） */
void* dma  = mem_dma_alloc(12800);   /* DMA 缓冲（显示/音频） */
void* big  = mem_psram_alloc(1 << 20); /* 大缓冲优先 PSRAM */
mem_free(big);                       /* 统一 mem_free 释放 */

/* 定长块池（高频固定尺寸对象：音频帧/事件体） */
static mem_pool_t* s_frame_pool;
s_frame_pool = mem_pool_create("spk_frame", 640, 16, MEM_MOD_AUDIO);
void* frame = mem_pool_alloc(s_frame_pool);     /* O(1)，池满返回 NULL */
mem_pool_free(s_frame_pool, frame);             /* 池块必须走 mem_pool_free！ */

/* 看账 */
mem_stat_print();                              /* 全模块表格 */
uint32_t cur = mem_stat_get(MEM_MOD_AUDIO)->cur;  /* 单桶当前值 */
```

注意：
- **ISR 里禁止调用任何 mem 接口**
- 池块不能传给 `mem_free`（会被 magic 校验拦截报错）
- 每笔分配有 8B 记账头，`mem_stat` 里的字节数含头开销
- 看自己的桶：`mem_stat_get(MEM_MOD_AUDIO)->cur / peak / allocs / fails`

### 3.3 event_bus —— 模块间通知（发布-订阅，解耦通道）

**事件类型编码**：`(MODULE_ID << 16) | EVENT_ID`。模块 ID 在 `event_bus_types.h`：
SYSTEM=0x0001, WIFI=0x0002, SENSOR=0x0003, TOUCH=0x0004, LORA=0x0005, UI=0x0006, AUDIO=0x0007, STORAGE=0x0008, DISPLAY=0x0009, WEB=0x000A。

**新增一个事件的三个动作**（都在 `event_bus_types.h`）：
1. `MODULE_ID_XXX` 已有就复用；没有才新增（**只增不删**）
2. 枚举里加 `EVENT_XXX_YYY = (MODULE_ID_XXX << 16) | 0x000N,`
3. 定义事件数据结构 `typedef struct {...} event_xxx_yyy_t;`

名表已自动化——枚举加完，`event_bus_get_type_name` 自动生效，无需登记。

**发布**（事件数据会被拷贝，发布后调用者可立即复用/释放）：

```c
#include "event_bus.h"

event_lora_data_received_t data = { .len = n, /* ... */ };
EVENT_BUS_PUBLISH(EVENT_LORA_DATA_RECEIVED, &data);   /* 自动 sizeof */
EVENT_BUS_PUBLISH_EMPTY(EVENT_LORA_SEND_COMPLETE);    /* 无数据 */
```

**订阅**（回调运行在**事件任务**上下文，返回 0 成功/非 0 计入 handler_errors）：

```c
static int on_lora_rx(const event_t* e, void* user_data) {
    const event_lora_data_received_t* d = (const void*)e->data;
    /* 纪律：回调里只做轻活（存状态/发消息/拷贝），重活投给 worker/lvgl_task */
    return 0;   /* 非 0 会计入 handler_errors 统计 */
}

event_subscription_t* sub = event_bus_subscribe(EVENT_LORA_DATA_RECEIVED, on_lora_rx, NULL);
/* 不用了必须退订，句柄置空 */
event_bus_unsubscribe(sub); sub = NULL;
```

注意：
- 回调内**允许**再订阅/发布/退订（5.1 修复后锁外调用，已由测试覆盖）
- 事件数据上限 **256B**；音频帧等大流量数据走队列/块池，事件只发元信息
- 队列 32 深，满了会丢弃并计 dropped；高频事件（如逐帧）不要走事件总线
- 诊断：`event_bus_print_status()` / `event_bus_print_subscribers()`

### 3.4 tasker —— 周期/延迟短任务

```c
#include "tasker.h"   /* 即 components/api/tasker_api/tasker.h */

static enum task_t my_poll(void* ctx) {
    /* 干活（必须 <500ms 且不阻塞） */
    return TASK_OK;      /* 非 TASK_OK 会 LOGW 并重试该任务 */
}

/* 栈上初始化 + 入队（move 语义：入队成功后源 node 被作废，勿再读写字段） */
struct task_node node;
tasker_task_init_mi(&node,
                    1000,          /* period ms：>0 周期任务，0 一次性 */
                    TASK_CNT_INF,  /* run_cnt：-1 无限，N 执行 N 次 */
                    "my_poll",     /* 名字须唯一（按名取消用） */
                    my_poll, NULL);
tasker_enqueue(&node);

/* 取消 */
tasker_cancel_by_name("my_poll");
```

三级选择（按时间成本）：

| 初始化函数 | level | 队列深度 | 默认超时 | 适合 |
|---|---|---|---|---|
| `tasker_task_init_li` | little | 8 | 50ms | 快查询、置位 |
| `tasker_task_init_mi` | middle | 8 | 1000ms | 传感器读取、状态刷新 |
| `tasker_task_init_lo` | lots | 8 | 10000ms | 文件操作类短活 |

注意：
- **超时只会把任务升级到更慢的 level，不会中断函数**——卡死的任务照样卡死
- **长任务（>1s）禁止进 tasker**，自建 FreeRTOS 任务/pthread（audio/web/lvgl 均如此）
- `priority`（first/middle/last）维度已废弃，不用填
- 周期任务完成/取消后，调度表在下次扫描自动回收节点

### 3.5 dtree —— 设备树读取（硬件参数唯一来源）

```c
#include "dtree.h"

/* 推荐：按 compatible 定位自己的节点（父节点即所属总线） */
dtree_node_t* dev = dtree_find_by_compatible("lora-module");
dtree_node_t* bus = dtree_get_parent(dev);      /* buses.uart1 */

/* 读属性（出参语义） */
int32_t aux_pin = 0;
dtree_get_int(dev, "aux_pin", &aux_pin);
const char* model = NULL;
dtree_get_string(dev, "model", &model);
bool invert = false;
dtree_get_bool(dev, "invert", &invert);

/* 路径直读便捷宏（不需要节点句柄时） */
int32_t w = 0;
DTREE_INT("buses.spi2.lcd_display", "width", &w);

/* 存在性判断 */
if (!dtree_has_node("buses.uart1.lora")) { /* 降级 */ }
```

设备树节点示例（`components/dtbs/config/ovs.dtb.json`）——你的三个模块节点**都已存在**：

| 节点 | compatible | 你的模块 |
|---|---|---|
| `buses.uart1.lora` | `lora-module`（含 `default_config` 子节点） | lora |
| `buses.i2c0.temperature_sensor` | `ath30-sensor`（含 `precision`） | ath30 |
| `buses.i2s0.amplifier` | `i2s-amplifier` | 扬声器 |

改设备树后 `idf.py build` 会重打 SPIFFS 镜像 + dtb.bin，OTA 推送即生效（A/B 槽自动切换）。

### 3.6 ovs_vfs —— 文件系统

```c
#include "ovs_vfs.h"
#include <stdio.h>       /* 挂载后直接用标准 C 文件 API */

/* 挂载已由 app_init 的 ovs_vfs 模块完成（按设备树 vfs.mounts），业务直接用路径 */
FILE* f = fopen("/audio/demo.wav", "wb");       /* /audio /media /font /config */
if (f) { fwrite(pcm, 1, n, f); fclose(f); }

/* 查询 */
bool ok = vfs_is_mounted("/audio/test.bin");
vfs_print_status();
```

注意：`/audio /media /font` 在外部 W25Q128 上，`/config` 在内部 Flash；分区重排过渡期挂载失败看 main/app_init 日志提示。

### 3.7 holder —— 启动编排（一般只碰 app_init.c）

- 新模块 = `app_init.c` 里加一个 `mod_xxx()` + 一行 `holder_register_module_ex(...)`（见第二章 2.5）
- `holder_init_all(true)`：required 失败即停启动（OTA 15s 回滚兜底）；optional 失败置 ERROR 继续
- 状态自动可见：开机串口表 + `GET /api/modules`（name/state/required/init_time_ms/error）
- `holder_is_module_ready("speaker")` 可在代码里查询别的模块是否就绪

### 3.8 web —— 加 API 端点与 WS 推送（components/modules/web/web.c）

```c
/* 1) 加路由（web.c 的 register_builtin_routes() 里） */
static void handle_speaker_vol(struct mg_connection *c, struct mg_http_message *hm) {
    /* hm->body 是请求体；用 cJSON 或 sscanf 解析 */
    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"vol\":50}");
}
r = (web_route_t){"POST", "/api/speaker/vol", handle_speaker_vol};
web_register_route(&r);

/* 2) 主动推送（任意线程可调，内部队列桥接到 web 任务） */
web_ws_broadcast("{\"ev\":\"SPEAKER_PLAY_DONE\"}", strlen(...));

/* 3) 订阅事件自动推送（参考 web.c 的 web_events_setup/on_event_ws_push） */
```

大文件上传/下载走 `web_register_stream_route()`（STREAM 流式路由，参考 `/api/ota/firmware`）。

---

## 四、总线驱动 API（你的三个模块各对应一条总线）

所有总线驱动遵循同一模式：`load_config(总线节点) → init(配置, &句柄) → 读写 → deinit`。**多设备共享同一总线时重复 init 同一总线是安全的（引用计数复用）**。

### 4.1 lora —— UART 总线（`buses.uart1.lora`）

```c
#include "uart_drv.h"

dtree_node_t* dev = dtree_find_by_compatible("lora-module");
dtree_node_t* bus = dtree_get_parent(dev);

uart_drv_config_t cfg;
uart_drv_load_config(bus, &cfg);          /* 引脚/波特率从设备树来 */
uart_drv_init(&cfg, &s_uart);

uart_drv_send(s_uart, frame, len);        /* 阻塞发送 */
uart_drv_receive(s_uart, buf, sizeof(buf), &rx_len);  /* 收（配合 AUX/M0/M1 GPIO） */
uart_drv_flush(s_uart);
```

lora 特有（M0/M1 配置模式、AUX 握手）属于你的驱动业务，M0/M1/AUX 直接用 `driver/gpio.h` 的 esp gpio API 直控（gpio 驱动组件按 C11 计划将删除，勿依赖）；注意 M0=GPIO8/M1=GPIO3/AUX=GPIO46/TXD=9/RXD=10（设备树为准）。接收建议改造成 **UART 事件 + 任务**（低延迟），不要 tasker 轮询。

### 4.2 ath30 —— I2C 总线（`buses.i2c0.temperature_sensor`）

```c
#include "i2c_drv.h"

dtree_node_t* dev = dtree_find_by_compatible("ath30-sensor");
uint8_t i2c_addr = 0x38;   /* 或从设备树属性读 */

i2c_drv_read_reg(s_i2c, i2c_addr, 0x00, raw, 6, &n);    /* 寄存器读 */
i2c_drv_write_reg(s_i2c, i2c_addr, reg, bytes, len);    /* 寄存器写 */
```

**现有 `modules/ath30` 就是一份完整参考实现**（含 CRC8 校验、测量触发、事件发布 `EVENT_SENSOR_TEMP_HUMIDITY`），重写时以它为对照基准；它 init 失败时 holder 会自动降级（当前因未接线处于 ERROR 状态，接线即恢复）。

### 4.3 扬声器 —— I2S 总线（`buses.i2s0.amplifier`）

```c
#include "i2s_drv.h"

/* 初始化见第二章 speaker.c 骨架；播放： */
size_t written = 0;
i2s_drv_write(s_i2s, pcm_chunk, chunk_bytes, &written);
```

建议架构（对齐方案第七节）：`speaker` 模块只做 I2S 直写 + 音量；将来 `audio_srv` 大管线（混音/编解码/队列）建立时吸收它。播放动作属于长任务——**自建任务或队列**，不要进 tasker。

---

## 五、线程与上下文纪律（最容易踩的坑）

| 上下文 | 规则 |
|---|---|
| 事件回调（event task） | 只做轻活：存状态/转发/投递。可再订阅/发布（已支持）；**不可阻塞、不可长时间计算** |
| tasker 任务 | ≤500ms、不阻塞；超时只会升级 level 不会中断 |
| lvgl_task | 所有 LVGL 调用必须在 lvgl_task 内；其他任务用 `lvgl_app_lock()/unlock()` |
| ISR | 禁止 mem_pool/logger/event_bus 全部接口；用 `gpio_evt_queue` 模式转任务 |
| 自建长任务 | >1s 的工作（音频流/解码/OTA 外的一切循环）自建任务；栈参考：4KB 常规、8KB 文件/解码、16KB LVGL |

模块间通信铁律：**跨模块通知走 event_bus；大流量数据走队列/块池；进 LVGL 用锁或 async**。禁止模块间互相 include 对方内部头。

---

## 六、验收清单（新模块完成的标准）

1. `idf.py build` 零新增警告；`ovs_tests`（`cmake --build build-tests && ./build-tests/ovs_tests`）89+ 用例全绿
2. OTA 推送后开机日志：你的模块出现在 holder 状态表（含 init 耗时），required 失败会回滚、optional 失败降级
3. `curl http://<ip>/api/modules` 能看到你的模块
4. 发了事件的：串口能看到订阅者收到；接了 WS 的：网页能看到推送
5. `mem_stat_print()` 里你的桶数字合理（无泄漏：启动后 cur 稳定）
6. `docs/development_log.md` 顶部记录（目标/完成/问题/下一步/变更）

---

## 七、常见坑速查

| 症状 | 原因 | 处置 |
|---|---|---|
| `malloc` 相关链接错误/账目乱 | 新组件忘了 `MEM_MODULE_TAG` 或用了裸 malloc | 统一 `mem_malloc`；CMake 加标签行 |
| `event_bus_get_type_name` 返回 UNKNOWN | 只加了枚举没重新构建 | 名表自动生成，重编译即可 |
| holder 停止启动 | required 模块 init 失败 | 看串口 holder 表的 Last error；确认该模块是否真该 required |
| tasker 任务不执行/执行慢 | 名字不唯一 / 长任务卡 level / 队列满（TASK_QUEUE_FULL 会提 pri 重试） | is_full 检查；换 level |
| 事件收不到 | 回调里取消了自己 / 队列满被丢弃（看 dropped 统计） | `event_bus_print_status()` |
| LVGL 崩溃/花屏 | 非_lvgl_task 线程直接碰 lv_obj | `lvgl_app_lock()` 包裹或 `lv_async_call` |
| 挂载失败提示格式化 | 分区布局变更过 | `OVS_MEDIA_FORMAT_ON_FIRST_BOOT=1` 烧一次 |
| WDT: IDLE 饿死 | 有任务在死循环 | 串口 backtrace + addr2line（参考 2026-09-09 tasker 自旋案例） |

---

*本文档由 Phase 1 收尾时整理；核心模块大改（如 event_bus/tasker）后请同步更新对应小节。*
