# 三任务并行开发计划：HUB / GUI / WEB

> 状态：接口冻结稿（§3 为对接契约，改动须走 task_board 请求流程）
> 配套：通用设计原则见 `docs/idle_modules_plan.md` §1（提示词中逐字引用）；
> 协作日志 `docs/task_board.md`（三任务共用共读）。
> 本文档取代 `docs/idle_modules_plan.md` §6 的 T1/T2/T3/T4 提示词：
> T1/T4 并入 [HUB]，T2 并入 [GUI]，T3 由 [GUI] 升级版取代，T5 归集成阶段。
> 原方案文档仍是各子任务的实现依据（音频 §2 / ath30 §3 / lora 两文档）。

---

## 1. 任务拆分与命名

| 任务名 | 职责 | 拥有的目录 | 吸收批次 |
|---|---|---|---|
| **[HUB] 数据中枢** | 两个 UI 共用的数据与服务层：i18n、传感器快照、系统/网络信息、时间服务；ath30 模块收口；LoRa 可靠传输层 | `components/core/{i18n,sensor_cache,time_svc}`、`components/modules/{sysinfo,ath30,lora_tp}`、`assets/i18n/*.json` | T1、T4 |
| **[GUI] 板端体验** | 板端蓝白主题 UI：主页(时间/温湿度/网络)、设置、待机接口、`_(label)` 多语言；音频输出升级（异步播放+播放器服务，UI 播放/音量直接对接） | `src/lvgl/**`、`components/modules/{audio_module,audio_player}` | T2、T3、T5(板端侧) |
| **[WEB] Web 界面** | Vue 前端重设计 + 后端新增只读 API 路由 + OTA 上传界面 | `web/vue-ui/src/**`、`components/modules/web/`（仅新增文件） | — |

**拆分逻辑**：两个 UI 需要同一批数据与翻译，集中到 HUB 供数；音频的消费者
是板端 UI（提示音/语音留言回放/音量），归 GUI 体验域；lora_tp/ath30 是
数据服务，归 HUB。三方互不依赖对方目录，HUB 未就绪时 GUI/WEB 用 mock
先行，互不阻塞。

## 2. 边界（越界=事故）

三方共同禁改：`components/modules/ota`、`components/modules/net_mgr`、
`components/drivers/**`、`scripts/**`、`main/`。

- **[HUB]** 只新增组件/文件 + 收口 ath30（允许改 ath30.c/h 实现，但
  ath30.h 现有函数签名不得变更）；lora_tp 只依赖 lora.h，禁改
  `components/modules/lora` 既有对外行为；`event_bus_types.h` 只追加；
  不碰 `src/lvgl`、`web/`、`modules/web`；
- **[GUI]** 动 `src/lvgl/**` 与 `components/modules/{audio_module,audio_player}`；
  audio_module 现有 API 保持兼容（录音路径不得回归）；禁 include 其他
  modules/* 业务头（数据经 bridge 注入，bridge 只 include HUB 公共头）；
  不碰 `components/core` 实现、`web/`、`modules/web`；
- **[WEB]** 前端只动 `web/vue-ui/src/**`；后端只在 `modules/web/` **新增**
  路由文件（如 web_api_sysinfo.c），不改 `web_ota.c` 既有逻辑（可新增
  additive 前置校验函数）、不改 `web_data` 打包机制；不碰 `src/lvgl`、
  `components/core`、其他 modules；
- **核心模块特殊 API 原则**（用户规则）：任何一方需要核心/共有模块新能力，
  不修改共有代码——在 task_board 发 `REQ[自己→HUB]`，由 HUB 以**新增**
  API 满足；紧急时也只允许 additive 文件。

## 3. 对接接口（冻结契约，HUB 实现，GUI/WEB 消费）

### I1 i18n（HUB 提供，GUI/WEB 共用同一翻译源文件）

```c
/* components/core/i18n/i18n.h */
i18n_err_t i18n_init(const char* default_lang);     /* 从 /i18n/<lang>.json 载入 */
i18n_err_t i18n_set_language(const char* lang);     /* 热切换，如 "zh-CN" */
const char* i18n_get(const char* label);            /* 未命中返回 label 原文+节流LOGW */
const char* i18n_current(void);
#define _(label) i18n_get(label)                    /* GUI 专用宏 */
```

- 翻译源：仓库 `assets/i18n/<lang>.json`，构建打包到设备 `/i18n/`；
  Web 经 `GET /i18n/<lang>.json` 取**同一份文件**（单一事实来源）；
- 文件 schema：`{"lang":"zh-CN","ver":1,"strings":{"HOME_TEMP":"温度",...}}`；
- 标签命名 `<页>_<部件>_<名>` 大写下划线；种子清单见附录 A；
- **追加规则**：三方均可 append 新键（附译文），禁止改/删他人键值。

### I2 传感器快照（HUB 提供，内部订阅 EVENT_ath30_DATA 缓存）

```c
/* components/core/sensor_cache/sensor_cache.h */
typedef struct {
    int32_t temp_m_c;     /* 毫摄氏度 */
    int32_t humi_m_p;     /* 毫%RH */
    uint32_t age_ms;      /* 距上次采样 */
    bool     valid;
} sensor_snapshot_t;
sensor_err_t sensor_snapshot_get(sensor_snapshot_t* out); /* 线程安全只读 */
```

### I3 系统/网络信息（HUB 提供的只读特殊 API，不改 net_mgr/wifi）

```c
/* components/modules/sysinfo/sysinfo.h */
typedef enum { NET_MODE_OFF=0, NET_MODE_STA, NET_MODE_AP } net_mode_t;
typedef struct {
    net_mode_t mode;  bool switching;
    char ssid[33];
    char ip[16]; char netmask[16]; char gw[16]; char mac[18];
    int8_t rssi;      /* AP 模式填 0 */
} net_info_t;
sysinfo_err_t sysinfo_net_get(net_info_t* out);
sysinfo_err_t sysinfo_device_get(sysinfo_dev_t* out); /* fw版本/运行时长 */
```

### I4 时间服务（HUB 提供，NTP 预留桩）

```c
/* components/core/time_svc/time_svc.h */
typedef struct {
    uint16_t year; uint8_t mon, day, hour, min, sec;
    bool synced;      /* NTP 同步成功标志，当前恒 false */
} time_svc_tm_t;
time_svc_err_t time_svc_get(time_svc_tm_t* out);
/* 预留（本期空实现/桩）：time_svc_ntp_enable(server)/time_svc_set() */
```

### I5 Web HTTP 契约（WEB 实现路由，数据取自 I2/I3/I4）

| 端点 | 方法 | 响应 data |
|---|---|---|
| `/api/sensor` | GET | `{"temp_c":26.5,"humi_p":48.2,"age_ms":1234,"valid":true}`（后端换算一位小数） |
| `/api/net/info` | GET | `{"mode":"sta","ssid":"...","ip":"...","netmask":"...","gw":"...","mac":"...","rssi":-52,"switching":false}` |
| `/api/time` | GET | `{"synced":false,"text":"12:34"}` |
| `/i18n/<lang>.json` | GET | 原样返回翻译文件 |
| `/api/ota/firmware`、`/api/ota/status` | 已有 | 不改动，前端直接使用 |

**固件合法性检查分工**：前端（WEB）上传前校验——扩展名 .bin、大小
≤OTA 分区、魔数（首字节 0xE9=纯 app，头 4 字节 "OVSO"=容器）不合法
直接拒绝不发；后端既有校验（ota_begin 长度/SHA256/回滚保护）不动。

### I6 待机接口（GUI 内部接口，本期立桩，未来画面接入用）

```c
/* src/lvgl/navigator/navigator.h */
lvgl_nav_err_t navigator_set_standby(screen_create_cb cb, uint32_t timeout_s);
/* cb=NULL 关闭待机；未来"接入画面"即实现一个 screen_create_cb 注册进来 */
```

### I7/I8 域内服务接口（不跨任务直连，跨任务只经 event_bus）

- **I7 lora_tp**（HUB 域内）：API 见 `docs/lora_transport_design.md` §2；
  对 GUI/WEB 只暴露 `EVENT_LORA_TP_TX_DONE/TX_FAILED/RX_COMPLETE/PROGRESS`
  事件（载荷 token+摘要），聊天页/通知接入走事件，不 include lora_tp.h；
- **I8 audio_player**（GUI 域内）：API 见 `docs/idle_modules_plan.md` §2；
  对外只暴露 `EVENT_AUDIO_PLAY_STARTED/DONE/FAILED`（载荷 token+err）；
  未来 WEB 若要远程播放，经 REQ[HUB] 加 HTTP 路由转调（本期不做）。

## 4. 协作机制（三任务共用共读 docs/task_board.md）

1. 每次会话**开始必读** task_board.md 末尾 30 行，**结束必追加**一条：
   `[任务名] 日期 | 状态 | 本会话产出 | 需要别人做的事`；
2. 跨任务请求格式：`REQ[GUI→HUB] 需要xxx接口/标签，原因，期望期限`；
   被请求方在下一会话响应并在 board 回 `DONE[HUB]`；
3. 发现接口契约需要变更（§3）→ board 发 `BREAK[任务名]` 说明，**不得
   先斩后奏**；三方都可能读到的公共文件（i18n json、event_bus_types.h）
   遵循"只追加"原则；
4. 惯例：HUB 尽先交付 I1~I4 可编译骨架（哪怕桩实现），GUI/WEB 全程
   可用 mock 推进，互不阻塞；各任务含多个子批次时，按提示词内"建议
   顺序"推进，跨会话续作以上一条目为交接；
5. **无板开发与成果文档制度**（本轮约束）：开发板不连接，验收=编译
  （idf.py build / PC 模拟 / vite build）+ PC 门禁（tests/ovs_tests），
  **不做任何真机验证**；真机测试指导统一写入共享成果文档
  `docs/hardware_test_guide.md`——每个任务只编写/更新自己章节
  （[HUB]=§1/§2，[GUI]=§3，[WEB]=§4），§5 已知限制 append-only，
  禁止改删他人章节；每会话结束若本章有新交付项，同步更新该文档。

---

## 附录 A：i18n 标签种子清单（HUB 建文件，中英初译；三方 append-only）

COMMON：APP_TITLE, BTN_OK, BTN_CANCEL, BTN_SAVE, BTN_BACK, BTN_REFRESH
HOME：HOME_TIME, HOME_TEMP, HOME_HUMI, HOME_NET_MODE, HOME_NET_STA,
HOME_NET_AP, HOME_NET_OFF, HOME_UNREAD
NET：NET_TITLE, NET_MODE, NET_SSID, NET_IP, NET_NETMASK, NET_GW,
NET_MAC, NET_RSSI, NET_SWITCHING
SETTINGS：SET_TITLE, SET_LANGUAGE, SET_VOLUME, SET_DEVICE_NAME, SET_STANDBY
SENSOR：SENSOR_NO_DATA
WEB：WEB_DASH_TITLE, WEB_NET_TITLE, WEB_FW_TITLE, WEB_FW_CURRENT,
WEB_FW_UPLOAD, WEB_FW_CHECKING, WEB_FW_SUCCESS, WEB_FW_FAIL,
WEB_FW_REBOOT, WEB_LANG

---

## 5. 任务提示词 [HUB] 数据中枢（含 T1 ath30 收口 + T4 lora_tp）

```
任务：实现 OVS 三任务并行开发中的 [HUB] 数据中枢，分三个子批次：
①统一数据接口（i18n/传感器快照/系统信息/时间服务）②T1 ath30 模块
收口 ③T4 LoRa 可靠传输层 lora_tp。跨多个会话完成，每会话结束在协作板
交接进度。

必读（按序）：
1. .agents/skills/esp32s3-smart-assistant/SKILL.md（规范/七步流程/日志）
2. docs/three_tasks_plan.md（§2 边界、§3 契约 I1~I4/I7、附录A 标签清单）
3. docs/task_board.md（协作日志，开始读末尾、结束必追加条目）
4. docs/idle_modules_plan.md（§1 通用设计原则逐字遵守；§3 为子批次②
   依据）
5. docs/lora_protocol.md 与 docs/lora_transport_design.md（子批次③依据，
   其 §9 为 lora_tp 详细提示词）
6. components/modules/ath30/、components/modules/lora/、
   components/core/event_bus/event_bus_types.h、src/app/app_init.c；
   docs/hardware_test_guide.md（三任务共享成果文档，结束更新[HUB]章节）

其他任务在做什么（你只提供接口，不得替它们实现）：
- [GUI] 拥有 src/lvgl 与 modules/{audio_module,audio_player}，在实装
  蓝白主题 LVGL 界面与音频播放服务，将调用你的 i18n(_宏)/
  sensor_snapshot_get/sysinfo_net_get/time_svc_get，并经 EVENT_LORA_TP_*
  事件接入未来聊天页；
- [WEB] 拥有 web/vue-ui 与 modules/web 新增路由，在重构 Vue 界面，
  将经 HTTP 用你的数据（它自己写路由，你不碰 modules/web）；
- 需要 core/共有模块新能力时它们会发 REQ[HUB]，你在 board 响应。

【设计原则】
1. 模块化：单一职责，公共接口最小化；组件不反向依赖应用层；
   模块间禁止直接 include 对方私有头，一律走 event_bus 或接口注入。
2. Linux 分层思想：驱动层（纯硬件操作，无策略）→ 核心服务层
   （机制 mechanism）→ 模块/应用层（策略 policy）。上层依赖下层的
   抽象接口而非实现；机制不管理策略，策略不碰硬件。
3. 错误处理：所有 API 返回错误码（组件前缀枚举，负值=错误）；资源
   申请统一 goto cleanup；错误必须 LOGW/LOGE 带上下文后上抛或降级，
   禁止静默吞错；可恢复错误不崩溃，optional 模块失败降级不阻塞启动。
4. 核心模块使用：事件通知走 event_bus；周期/延时工作走 tasker（组件内
   禁建 FreeRTOS 任务）；日志统一 logger.h 分级 LOGD/I/W/E；硬件参数从
   设备树 DTREE_* 读取，禁止硬编码；模块接入走 app_init 注册表+holder
   编排（optional 模块缺席自动降级）；文件走 ovs_vfs；大块内存走项目
   内存池，禁裸 pvPortMalloc。
5. 可移植性：组件公共头禁止 esp/FreeRTOS 平台头；平台能力经依赖注入；
   自检清单 SKILL.md §7.10。
6. 验证纪律：每次交付必须跑 PC 门禁 tests/ovs_tests + idf.py build，
   双绿才算完成；完成后更新 docs/development_log.md 顶部条目。

工作项（按子批次顺序，跨会话可拆）：
【批次① 统一数据接口】
1. components/core/i18n/：契约 I1（cJSON 解析 flat KV、热切换、未命中
   回退原文+节流LOGW、_(label) 宏）；翻译源 assets/i18n/zh-CN.json+
   en-US.json（附录A 种子清单建文件+中英初译），构建打包至 /i18n/
   （参照 ovs.dtb.json 的 SPIFFS 镜像打包先例）；
2. components/core/sensor_cache/：契约 I2；订阅 EVENT_ath30_DATA 缓存
   最新值，get 返回快照+age_ms；
3. components/modules/sysinfo/：契约 I3；只读查询 esp_netif/esp_wifi，
   不改 net_mgr/wifi 任何文件；
4. components/core/time_svc/：契约 I4；synced 恒 false，NTP 桩接口
   留空实现+TODO 注释；
5. 四组件 app_init 注册（均 optional，缺席降级）；event_bus_types.h
   只允许追加；tests/ovs_tests：i18n（加载/切换/回退/append）、
   sensor_cache（注入事件→快照）、sysinfo/time_svc（PC mock）全绿。
【批次② T1 ath30 收口】（依据 idle_modules_plan §3，勿重写已有实现）
6. 确认/补全 EVENT_ath30_DATA/ERROR 事件注册与载荷（温度/湿度用
   int32 milli 单位）；采集走 tasker Middle 周期任务，间隔读设备树
   sample_interval_ms（缺省 30000+LOGW）；
7. CRC8 校验确认；连续失败 3 次→发 ERROR 事件并停采，之后每 5 个
   周期重试恢复（LOGW）；ath30.h 现有签名不得变更；
8. mock i2c 单测三场景：正常读取/无应答超时/CRC 错误，事件载荷断言。
【批次③ T4 lora_tp 传输层】（依据 lora_transport_design.md §2/§3 API
   与 §9 提示词执行，此处不重复）
9. 新组件 components/modules/lora_tp/：异步会话制 send/取消/查询、
   source/sink 抽象、滑窗 ARQ+CRC32+断点续传、UNRELIABLE 直通广播；
10. mock 驱动回环测试覆盖 lora_transport_design §7 六场景（成功/丢帧
    30%重传/断链续传/CRC 注入/广播限制/大消息 sink 路径）；真机联调
    归集成阶段，LEVEL 档位数值用占位值并注释标记（不阻塞 PC 开发）；
11. 设备树 lora.lora_tp 配置节点（契约见 lora_transport_design §5，
    DTREE_* 读取+缺省兜底）。

边界：禁改 web/、src/lvgl、modules/web、modules/net_mgr、modules/ota、
modules/lora 现有对外行为；ath30 仅限收口所需改动且签名不变；需要改
共有代码时只在 board 说明并以新增 API 满足。
【无板约束】本轮开发板不连接：不做任何真机验证，全部验收止于编译与
PC 门禁；真机测试步骤写入 docs/hardware_test_guide.md 供项目所有者
后续上板执行。
验收（每子批次）：ovs_tests 全绿 + idf.py build 通过；更新
development_log.md + task_board.md 条目（写明契约交付状态供 GUI/WEB
集成、子批次进度与下一步）；同步更新 hardware_test_guide.md 的
[HUB]章节（§1 全局前置/§2 各测试项：前置条件、步骤、预期串口日志
关键字、通过标准、排查点）。
```

## 6. 任务提示词 [GUI] 板端体验（LVGL + T2 音频）

```
任务：实现 OVS 三任务并行开发中的 [GUI] 板端体验，分两个子批次：
①T2 音频输出升级（audio_module 异步化 + 新增 audio_player 播放服务）
②LVGL 六层 UI 实装（蓝白主题/多语言/主页/设置/待机接口）。跨多个会话
完成，每会话结束在协作板交接进度。

必读（按序）：
1. .agents/skills/esp32s3-smart-assistant/SKILL.md；src/lvgl/README.md
   （六层职责/脚手架现状）；src/lvgl/lvgl_app.c 与 port/
2. docs/three_tasks_plan.md（§2 边界、§3 契约 I1/I2/I3/I4/I6/I8、
   附录A 标签清单）
3. docs/task_board.md（开始读、结束追加）；docs/idle_modules_plan.md
   （§1 通用设计原则逐字遵守；§2 为子批次①依据、§4 为②参考）
4. components/modules/audio_module/（现有实现，演进勿重写）、
   components/drivers/i2s_drv/i2s_drv.h、components/drivers/gpio/、
   components/dtbs/config/ovs.dtb.json；
   docs/hardware_test_guide.md（三任务共享成果文档，结束更新[GUI]章节）

其他任务在做什么（涉及模块，你不得代改）：
- [HUB] 拥有 components/core/{i18n,sensor_cache,time_svc}、
  components/modules/{sysinfo,ath30,lora_tp}，在实现统一数据接口/
  ath30 收口/LoRa 传输层——你 include 其公共头（i18n/sensor_cache/
  sysinfo/time_svc）；未交付前 bridge 用 mock 常量（同签名假数据），
  交付后仅换 bridge 实现接真；聊天页/通知未来经 EVENT_LORA_TP_* 事件
  接入，你不 include lora_tp.h；
- [WEB] 拥有 web/vue-ui 与 modules/web 新增路由，与你的主题色/字段/
  翻译文件保持一致但代码零共享；新翻译标签在 task_board 追加。

【设计原则】
（同 [HUB] 提示词六条通用设计原则，逐字粘贴）
【UI 铁律】ui/widgets/pages 三层零业务头文件（只 include lvgl.h 与
层内头）；业务依赖只出现在 presenters/bridge；pages 禁止轮询阻塞，
刷新一律事件/LVGL timer 投递；多语言一律 _(label) 宏，禁止硬编码文案。

工作项：
【批次① T2 音频】（分层严格按 idle_modules_plan §2.2：i2s_drv→
  audio_module=PCM 设备抽象→audio_player=播放器策略）
1. audio_module 演进（现有 init/deinit/录音 API 保持兼容，录音路径
   不得回归）：新增 audio_module_write_async()（环形缓冲+tasker Little
   喂数）、set_volume(0~100)/get_volume、mute(bool)（SD 引脚经 gpio
   驱动）、drain/abort、播放完成回调；阻塞 play() 保留标 deprecated；
   软件音量=int16 饱和缩放；
2. 新增 components/modules/audio_player/：播放队列（深度≥3）、文件
   播放（ovs_vfs，裸 PCM 头：magic/rate/ch/bits/len）、audio_decoder_t
   解码挂点（v1 注册 passthrough，未来 codec2 即插）、音量 NVS 持久化；
   API：play_file/play_mem/stop/set_volume/get_volume+token 查询；
3. event_bus_types.h 追加 EVENT_AUDIO_PLAY_STARTED/DONE/FAILED（载荷
   token+err+duration_ms），进度事件节流 ≥10%（追加前在 board 声明）；
4. 设备树 audio 节点补 sd_pin/volume_default/queue_depth 键（DTREE_*
   读取+缺省兜底）；mock i2s 单测覆盖 idle_modules_plan §2.4 全场景
   （异步完成事件/音量缩放/stop-drain 语义/队列依次播放）。
【批次② LVGL UI】
5. themes/base：蓝白主题——主蓝 #1E6FFF 系、白卡片、#F5F7FA 页面底、
   深色文本三级（主/次/占位），样式常量集中定义，美观简洁；
6. ui/controls+indicators：card（图标+主值+副值）、list_item、
   round_btn（按住语义预留）、status_dot、值标签（含单位）；
7. navigator：注册表+push/pop/switch+生命周期；实现契约 I6
   navigator_set_standby(cb,timeout) 与 pages/standby 占位页（未来经
   cb 注入任意画面）；
8. pages：home（时间 HH:MM——time_svc，synced=false 照常显示并灰显；
   温湿度卡——sensor_cache；网络卡——sysinfo 模式/IP/RSSI，字段与 WEB
   /api/net/info 一致；未读角标占位 0）、settings（音量 slider 实调
   audio_player_set_volume——批次①成果；语言切换 i18n_set_language
   即时刷新；设备名占位）、standby；LVGL timer 1s 仅刷时间，其余事件
   驱动；提示音钩子：消息类事件到达调 audio_player_play_mem 播放内置
   提示音（可选，后置）；
9. presenters+bridge：bridge 定义 I1~I4 函数表（唯一 include HUB 头
   的地方），真身+mock 两套（编译开关）；
10. navigator 栈纯逻辑进 tests/ovs_tests；grep 自检三层无违规 include。

边界：只动 src/lvgl/**、components/modules/{audio_module,audio_player}
（根 CMake 注册除外）；禁改 components/core 实现、web/、modules/web、
modules/net_mgr、modules/ota；要新标签→task_board append；要新接口→
REQ[GUI→HUB]。
【无板约束】本轮开发板不连接：不做任何真机验证（外放试听、触摸显示
等全部写入测试指南供后续上板）；音频验收=mock i2s 单测+编译，UI 验收
=PC 模拟器+编译。
验收（每子批次）：ovs_tests 全绿 + idf.py build 与 PC 模拟 build 双绿；
批次②另验三页导航/主题/语言热切换/mock 数据显示（PC 模拟器内）；
更新 development_log.md + task_board.md 条目（交接子批次进度）；同步
更新 hardware_test_guide.md 的[GUI]章节（§3 音频/LVGL 各测试项：前置、
步骤、预期表现与串口日志、通过标准、排查点）。
```

## 7. 任务提示词 [WEB] Web 界面

```
任务：重构并美化 OVS 的 Web 上位机（Vue）：模块化前端、多语言、
温湿度/网络信息展示、OTA 固件上传升级界面（复用既有 OTA 接口，
前后端双重合法性检查）。

必读（按序）：
1. .agents/skills/esp32s3-smart-assistant/SKILL.md（构建流程含 Vue
   前端打包，mybuild/ovs_release 链路勿破坏）
2. docs/three_tasks_plan.md（§2 边界、§3 契约 I2/I3/I4/I5 及固件检查
   分工、附录A 标签清单）
3. docs/task_board.md（开始读、结束追加）；docs/idle_modules_plan.md §1
4. web/vue-ui/（现有工程：vite+App.vue）、components/modules/web/web.c
   （路由注册表）、web_ota.c（OTA 上传既有实现，只读参考）、
   web_data/（dist 打包机制）；
   docs/hardware_test_guide.md（三任务共享成果文档，结束更新[WEB]章节）

其他任务在做什么（涉及模块，你不得代改）：
- [HUB] 拥有 components/core/{i18n,sensor_cache,time_svc}、
  components/modules/{sysinfo,ath30,lora_tp}，在实现统一数据接口/
  ath30 收口/LoRa 传输层——你的后端路由直接调 sensor_snapshot_get/
  sysinfo_net_get/time_svc_get（签名在 three_tasks_plan §3）；未交付前
  路由内返回 mock JSON 先行；
- [GUI] 拥有 src/lvgl 与 modules/{audio_module,audio_player}，在做板端
  LVGL 界面与音频服务，与你的主题/字段/翻译文件保持一致但代码零共享；
  翻译源 /i18n/<lang>.json 与它共用，你负责 HTTP 服务该文件。

【设计原则】
（同 [HUB] 提示词六条通用设计原则，逐字粘贴）
【前端特别原则】Vue 组件模块化：目录 src/{api,components,composables,
i18n,router,stores,views}；api 层集中封装 fetch（统一错误处理/code
判型）；视图组件单一职责、可复用 UI 拆 components/；状态用轻量 store
（不引重型框架则自研 composable）；文案零硬编码，全走 i18n 键。

工作项：
1. 前端重构（蓝白主题与 GUI 一致：主蓝 #1E6FFF、白卡片、简洁留白）：
   - Dashboard：温湿度卡（GET /api/sensor，30s 轮询+手动刷新）、
     网络模式与配置卡（GET /api/net/info：mode/ssid/ip/netmask/gw/
     mac/rssi）；
   - Network 页：现有 /api/wifi/scan|connect|status 与 /api/net/mode
     的界面化（模式切换、选网连接）；
   - Firmware 页（OTA）：选文件→前端校验（.bin、≤分区大小、魔数
     首字节 0xE9 或头部 "OVSO"，非法拒绝并提示）→POST /api/ota/firmware
     （复用，后端不改）→轮询 GET /api/ota/status 展示进度/结果/
     重启倒计时；显示当前固件版本；
   - Settings：语言切换（i18n 即时生效）、其余占位；
   - 多语言：zh-CN/en-US 起，语言包 fetch('/i18n/<lang>.json')（与
     LVGL 同源），缺键回退英文、再回退键名；
2. 后端新增（仅 additive 文件，如 modules/web/web_api_sysinfo.c）：
   按契约 I5 实现 GET /api/sensor、/api/net/info、/api/time 与
   GET /i18n/<lang>.json（SPIFFS 读取，路径白名单防穿越）；不改
   web_ota.c 既有逻辑与 modules/ota；
3. 构建验证：vite build 产物经既有 web_data 打包链路可编译进固件。

边界：只动 web/vue-ui/src/** 与 modules/web/ 新增文件；禁改 src/lvgl、
components/core、modules/{ota,net_mgr,ath30,lora_tp,audio_*}；要新数据→
REQ[WEB→HUB]。
【无板约束】本轮开发板不连接：不做任何真机验证；验收=vite dev（mock
数据）+ vite build + idf.py build；上板测试步骤（含 curl 端点自测与
OTA 网页上传流程）写入测试指南供后续执行。
验收：本地 vite dev 用 mock 联调全页面可用；vite build + idf.py build
通过（含新路由与打包）；更新 development_log.md + task_board.md 条目；
同步更新 hardware_test_guide.md 的[WEB]章节（§4：访问入口、各页面
测试步骤、curl 端点自测命令、OTA 上传实测流程与预期、通过标准）。
```

---

## 8. 原批次去向对照

| 原批次 | 去向 |
|---|---|
| T1 ath30 收口 | [HUB] 子批次②（§5） |
| T2 音频升级 | [GUI] 子批次①（§6） |
| T3 UI 框架 M1 | 废弃，由 [GUI] 子批次②（升级版）取代 |
| T4 lora_tp | [HUB] 子批次③（§5，详细依据 lora_transport_design §9） |
| T5 UI M2/M3 + 真机联调 | [GUI] 批次②内 + 三任务集成阶段（task_board 协调） |
