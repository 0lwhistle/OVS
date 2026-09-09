# LoRa 传输服务层（lora_tp）设计方案

> 状态：设计定稿，待实现（实现提示词见 §9）
> 上游文档：`docs/lora_protocol.md`（链路层帧格式/ARQ/语音仲裁，本文引用不重复）
> 下游依赖：`components/modules/lora`（现有驱动：UART 收发 + AUX 流控 + AT 配置层）
> 本文只定义**分层边界与公共 API**，实现细节以协议文档 v2 为准。

---

## 0. 需求原文与设计回应

> lora 模块暴露传输接口，接口参数包括固定格式的 data 和传输目的对象 des，
> 应用通过接口传输任意数据，不关心驱动和模块实现；接收同理。传输可靠
> （分片、校验、断点续传）。模块化封装后，应用可自行封装协议。

对应设计决策：

| 需求 | 决策 |
|---|---|
| 固定格式 data，传输任意数据 | 载荷对 lora_tp **完全透明**（opaque bytes），格式由应用自定义；lora_tp 只在外层加传输头 |
| 目的对象 des | `lora_tp_des_t`：地址 + QoS + 档位 |
| 不关心驱动实现 | 依赖注入：应用只见 lora_tp.h；lora_tp 只依赖 lora.h 现有平台无关 API |
| 可靠（分片/校验/续传） | 默认 QoS_RELIABLE：自动分片+滑窗 ARQ+消息级 CRC32+断点续传 |
| 应用自行封装协议 | lora_tp 不解析载荷、无业务命令概念；协议文档 §4 命令层降级为"应用层第一个协议示例"，不进本模块 |

## 1. 分层与组件边界

```
应用层            自定义载荷格式、会话语义（如聊天、指令）
lora_tp（新组件）  components/modules/lora_tp/
                  传输会话：分片/滑窗ARQ/CRC32/续传/仲裁/收端重组
lora（现有驱动）   components/modules/lora/
                  UART 原始收发 + AUX 流控 + AT 配置层 + 三态帧解析
DX-LR22           PHY CRC、230B 分包、透明传输
```

边界铁律：
- lora_tp **不碰** UART/M0/M1/AUX，不认识 AT 指令——只调 lora.h 的
  `lora_send/lora_register_rx_callback`；
- lora **不理解**帧内容——只做字节流的收发与帧边界解析；
- 应用 **不接触** lora.h——只 include lora_tp.h（调试用途除外）。

## 2. 公共 API（lora_tp.h 草案，平台无关）

```c
/* ===== 错误码（沿用项目负值惯例） ===== */
typedef enum {
    LORA_TP_OK = 0,
    LORA_TP_ERR_PARAM     = -1,
    LORA_TP_ERR_NOT_INIT  = -2,
    LORA_TP_ERR_BUSY      = -3,   /* 发送队列满 */
    LORA_TP_ERR_NO_MEM    = -4,
    LORA_TP_ERR_IO        = -5,   /* 数据源读失败 */
    LORA_TP_ERR_TIMEOUT   = -6,   /* 对端无响应（已重试耗尽） */
    LORA_TP_ERR_PEER      = -7,   /* 对端拒绝/收端落盘失败 */
    LORA_TP_ERR_CANCELLED = -8,
    LORA_TP_ERR_TOO_LARGE = -9,   /* 超过单消息上限或收端无法接收 */
} lora_tp_err_t;

/* ===== QoS 与目的对象 ===== */
typedef enum {
    LORA_TP_QOS_RELIABLE   = 0,   /* 分片+ARQ+CRC32+续传（默认） */
    LORA_TP_QOS_UNRELIABLE = 1,   /* 单帧直发，尽力而为（语音流/广播） */
} lora_tp_qos_t;

#define LORA_TP_ADDR_BCAST  0xFFFFu

typedef enum {
    LORA_TP_PROFILE_NORMAL = 0,   /* 中档：常规文本 */
    LORA_TP_PROFILE_FAST   = 1,   /* 高档：语音/文件（档位映射待手册核实） */
    LORA_TP_PROFILE_SLOW   = 2,   /* 低档：远距短文本 */
} lora_tp_profile_t;

typedef struct {
    uint16_t          addr;       /* 目标地址；0xFFFF 广播（仅 UNRELIABLE） */
    lora_tp_qos_t     qos;
    lora_tp_profile_t profile;
} lora_tp_des_t;

/* ===== 数据源/数据汇抽象（内存与文件统一，依赖注入核心） ===== */
typedef struct {
    void*         ctx;
    /* 从 offset 读 len 字节；*out 为实际读取数。由调用方实现并保证生存期 */
    lora_tp_err_t (*read)(void* ctx, uint32_t offset,
                          void* buf, size_t len, size_t* out);
} lora_tp_source_t;

typedef struct {
    void*         ctx;
    /* 把 len 字节写到 offset（收端重组落盘：RAM 环或 ovs_vfs 文件） */
    lora_tp_err_t (*write)(void* ctx, uint32_t offset,
                           const void* buf, size_t len);
} lora_tp_sink_t;

/* ===== 发送 ===== */
typedef uint32_t lora_tp_token_t;   /* 会话令牌，>0 有效 */

/**
 * @brief  发送一段任意数据（异步，立即返回）
 * @param  des    目的与 QoS
 * @param  src    数据源（内存/文件由 source 抽象统一）
 * @param  total  总字节数；RELIABLE 上限 LORA_TP_MSG_MAX(默认 64KB)，
 *                UNRELIABLE 上限单帧载荷 200B
 * @param  token  出参：会话令牌，用于查询/取消
 * @note   非阻塞：数据在发送过程中必须保持可读（source 生存期覆盖整个会话）
 */
lora_tp_err_t lora_tp_send(const lora_tp_des_t* des,
                           const lora_tp_source_t* src, uint32_t total,
                           lora_tp_token_t* token);

lora_tp_err_t lora_tp_cancel(lora_tp_token_t token);

/* 会话进度查询（0~1000 千分比；状态：进行中/完成/失败及失败原因） */
typedef enum {
    LORA_TP_ST_RUNNING, LORA_TP_ST_DONE,
    LORA_TP_ST_FAILED,  LORA_TP_ST_CANCELLED,
} lora_tp_state_t;
lora_tp_err_t lora_tp_query(lora_tp_token_t token,
                            lora_tp_state_t* st, uint32_t* permille);

/* ===== 接收 ===== */
typedef struct {
    uint16_t        src;        /* 对端地址 */
    uint16_t        dst;        /* 本机或广播 */
    uint32_t        msg_id;
    uint32_t        len;
    lora_tp_qos_t   qos;
    const void*     data;       /* 非NULL：小消息连续缓冲，回调返回后失效 */
} lora_tp_rx_info_t;

/**
 * @brief 注册收包通知（收端重组完成、整条校验通过后回调一次）
 * @note  大消息（> LORA_TP_RX_INLINE_MAX，默认 2KB）data 为 NULL，
 *        应用须事先 lora_tp_set_rx_sink() 提供落盘位置，否则拒收
 *        （对端收到 ERR_PEER 而失败，不会白传）
 */
typedef void (*lora_tp_rx_cb_t)(const lora_tp_rx_info_t* info, void* user);
lora_tp_err_t lora_tp_register_rx(lora_tp_rx_cb_t cb, void* user);
lora_tp_err_t lora_tp_set_rx_sink(const lora_tp_sink_t* sink); /* NULL=恢复内建RAM */

/* ===== 生命周期与运维 ===== */
lora_tp_err_t lora_tp_init(const struct lora_tp_deps* deps); /* 见 §4 */
lora_tp_err_t lora_tp_deinit(void);
typedef struct {
    uint32_t tx_ok, tx_fail, rx_ok, rx_dup, rx_crc_err;
    uint32_t frames_tx, frames_retx;
} lora_tp_stats_t;
lora_tp_err_t lora_tp_get_stats(lora_tp_stats_t* s);
```

**事件总线**（对 UI/后台的异步通知，订阅式，与回调二选一或并用）：
`EVENT_LORA_TP_TX_DONE / TX_FAILED / RX_COMPLETE / PROGRESS(节流5%)`，
载荷为 token + lora_tp_rx_info_t 摘要。

## 3. 关键语义

1. **异步会话制**：send 只入队（内部 FIFO 深度 4，满则 BUSY）；同一时刻
   只有一个 RELIABLE 会话在传（半双工决定），队列按序服务；
2. **载荷透明**：lora_tp 对 data 零解析；msg_id/CRC32/分片信息全部在
   传输子头（协议文档 §5.1 扩展：+4B CRC32）；
3. **可靠性**：滑窗 W=4 + 累积 ACK + NACK 位图 + 整窗重试 3 次 +
   断点续传（收端 ACK 回带最高连续 offset，发端从 offset 续读 source）；
4. **校验双层**：PHY CRC（模组，丢坏包）+ 消息级 CRC32（端到端，
   防重组逻辑错误）；收端先写临时区、CRC 通过才算 RX_COMPLETE；
5. **广播限制**：QOS_UNRELIABLE + addr=0xFFFF 才允许广播；可靠广播
   （多端 ACK 收敛）明确不支持，文档注明；
6. **仲裁**：协议文档 §6.3 语义——UNRELIABLE(PRI) 帧活跃时挂起
   RELIABLE 会话，静默 500ms 后自动续传，应用无感知；
7. **幂等**：收端按 (src, msg_id) 去重，重复消息不再回调。

## 4. 依赖注入与可移植性（强制项）

```c
struct lora_tp_deps {
    const lora_drv_api_t* drv;          /* 下层驱动接口（v1 即 lora.h 包装） */
    uint32_t (*tick_ms)(void);          /* 平台时钟 */
    void     (*delay_ms)(uint32_t);     /* 平台延时 */
    void*    (*mem_alloc)(size_t);      /* 内存池接口（项目已建池） */
    void     (*mem_free)(void*);
};
```

- lora_tp.h **禁止**出现 esp/、FreeRTOS、lvgl 任何头（可移植性检查清单 §7.10）；
- `lora_drv_api_t` 是对现有 lora.h 函数的薄包装（函数指针表），
  **为 PC 模拟回环测试留桩口**：测试用 mock 驱动把帧直通另一个 lora_tp 实例，
  全链路 ARQ/续传逻辑可在 PC 上跑 tests/ovs_tests；
- 定时驱动：tasker 周期任务 tick（10ms Little 级），lora_tp 内部不建
  FreeRTOS 任务、不 sleep 轮询。

## 5. 配置（设备树，不硬编码）

ovs.dtb.json 的 lora 节点下扩展：

```json
"lora_tp": {
    "tx_queue_depth": 4,
    "window": 4,
    "max_retries": 3,
    "msg_max_bytes": 65536,
    "rx_inline_max_bytes": 2048,
    "ack_timeout_ms_profile": [350, 110, 5700],
    "tx_gap_ms_profile": [300, 60, 5600]
}
```

（数值对应 NORMAL/FAST/SLOW，最终以手册核实的 LEVEL 映射修正；
ack_timeout 公式见协议文档 §8。）

## 6. 错误处理与状态机

- 所有 API 参数判空、未初始化判型，goto cleanup 惯例；
- 会话状态机：`IDLE→QUEUED→HANDSHAKE(可选VM通知)→TRANSFER→CLOSING→DONE/FAILED`，
  每次迁移写 LOGD，失败迁移 LOGW 带 errCode；
- 发送中 source 读失败 → 会话 FAILED(ERR_IO)，不在驱动层重试 IO；
- 收端 sink 写失败 → ACK 带错误码，对端干净收尾（不重传灌死对端）；
- 断电恢复：发送会话不持久化（应用层用 outbox 模式重建）；收端 .tmp
  落盘文件 + msg_id 支持跨断电续传（协议文档 §9）。

## 7. 验收标准

1. PC 门禁：tests/ovs_tests 新增 lora_tp 用例全绿——mock 驱动回环下：
   - 8KB 内存消息传输成功、内容逐字节一致；
   - 人为丢帧 30% → 重传后仍成功，rx_dup 正确计数；
   - 中途"断链"（mock 停止转发 N 秒）→ 恢复后从断点续传完成；
   - CRC 注入错误 → 收端拒收、对端 FAILED(PEER)；
   - 广播 UNRELIABLE 单帧直达；RELIABLE 广播被拒（ERR_PARAM）；
   - 50KB 消息触发 sink 路径，RAM 路径超限被拒（ERR_TOO_LARGE）；
2. idf.py build 通过，板端 lora 驱动现有行为不回归；
3. lora_tp.h 通过可移植性检查清单（无平台头文件、无硬编码引脚/速率）。

## 8. 实现步骤（项目模块七步流程）

1. `components/modules/lora_tp/` 新建 lora_tp.h/.c + CMakeLists；
2. event_bus_types.h 增加 LORA 模块事件；
3. 依赖注入结构 + lora.h 函数指针包装；
4. 会话管理与分片状态机（先 RELIABLE 单会话，UNRELIABLE 直通）；
5. ARQ/续传/CRC32；6. 收端重组与 sink；7. 接 event_bus + tasker tick +
   dtree 配置 + tests/ovs_tests 回环用例。

---

## 9. 实现提示词（后续会话直接投喂）

```
任务：按设计文档实现 OVS 的 LoRa 传输服务层组件 lora_tp。

必读（按序）：
1. .agents/skills/esp32s3-smart-assistant/SKILL.md（项目规范、七步流程、日志要求）
2. docs/lora_transport_design.md（本次实现的设计依据，API 以其 §2 为准）
3. docs/lora_protocol.md（链路层帧格式 §2/§3、ARQ §5、仲裁 §6.3、档位 §8）
4. components/modules/lora/lora.h（下层驱动现有 API；lora.c 可参考但不修改
   其对外行为）
5. docs/development_log.md 顶部两条（当前未提交上下文）

实现要求：
- 新组件 components/modules/lora_tp/（lora_tp.h/.c/CMakeLists.txt），根
  CMakeLists 注册，src/app/main.c 按 holder/app_init 模式接入（optional 模块，
  lora 驱动缺席时降级不注册）；
- API 与语义严格按 docs/lora_transport_design.md §2/§3，不得增删公共接口；
- 可移植性硬约束：lora_tp.h 不含 esp/FreeRTOS 头；依赖经 lora_tp_deps_t
  注入；内存走 deps 注入的池接口；定时由 tasker 周期任务驱动，组件内不建
  FreeRTOS 任务；错误处理 goto cleanup；日志用 logger.h（TAG="[LORA_TP]"）；
- 设备树配置按设计 §5 增补 ovs.dtb.json 的 lora.lora_tp 节点并用 DTREE_*
  读取，缺省值兜底（树中无节点也能工作）；
- event_bus_types.h 新增 EVENT_LORA_TP_* 四事件；
- 测试：tests/ovs_tests 新增 mock 驱动回环套件，覆盖设计 §7 验收的 6 个
  场景（成功/丢帧重传/断点续传/CRC错误/广播限制/大消息sink路径），
  PC 门禁必须全绿；板端先只保证 idf.py build 通过（真机联调另起任务）；
- 明确不做：语音流编码、业务命令层（聊天/指令协议属应用层）、AT 配置层
  重写（另列任务）；LEVEL 档位数值用手册核实前的占位值并注释标记；

完成后：运行 PC 测试与 idf.py build，按 SKILL.md 规范在
docs/development_log.md 顶部追加条目（含验收结果与待核实项）。
```
