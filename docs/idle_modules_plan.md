# 闲时模块开发计划：LoRa / 音频(MAX98357A) / AHT30 / LVGL UI

> 状态：需求与方案定稿，实现提示词随附（§6），按闲时任务批次执行
> 配套文档：LoRa 部分见 `docs/lora_protocol.md`（链路协议）与
> `docs/lora_transport_design.md`（传输服务层，内含 lora 实现提示词）
> 命名勘误：真实芯片为奥松 **AHT30**，docs/ 下 7 处 "ATH30" 拼写已于
> 2026-09-09 统一修正；代码组件 components/modules/aht30 本就正确。

---

## 0. 现状盘点与需求

| 模块 | 现状 | 需求 | 缺口 |
|---|---|---|---|
| lora | 协议 v2 + 传输层设计定稿，代码未动 | 文本消息/语音留言/指令（§lora 两文档） | 全部实现 + 手册核实 |
| 音频 MAX98357A | audio_module 有阻塞式录音/播放基础（344行） | 提示音、语音留言回放、音量控制、异步播放 | 异步化/音量/完成事件/文件播放/codec2 挂点 |
| AHT30 | 已实现（430行）+ event_bus 发布 + app_init 注册 | 温湿度采集显示、UI 数据通道 | 审计收口 + 单测 + 事件规格确认 |
| LVGL UI | 六层架构仅脚手架，仅 lvgl_app.c 编译 | 主页/LoRa 会话/设置页，事件驱动刷新 | 全部实装（分里程碑） |

产品目标（需求源头）：微信式语音/文本对话（见 lora 文档）、桌面环境显示、
可导航的触摸 UI。UI 页面依赖事件，不直接依赖模块——四个模块可并行开发。

## 1. 通用设计原则（所有提示词共享，逐字复制进每个提示词）

```
【设计原则】
1. 模块化：单一职责，公共接口最小化；组件不反向依赖应用层；
   模块间禁止直接 include 对方私有头，一律走 event_bus 或接口注入。
2. Linux 分层思想：驱动层（纯硬件操作，无策略）→ 核心服务层
   （机制 mechanism）→ 模块/应用层（策略 policy）。上层依赖下层的
   抽象接口而非实现；机制不管理策略，策略不碰硬件。
3. 错误处理：所有 API 返回错误码（组件前缀枚举，负值=错误）；资源
   申请统一 goto cleanup；错误必须 LOGW/LOGE 带上下文后上抛或降级，
   禁止静默吞错；可恢复错误不崩溃，optional 模块失败降级不阻塞启动。
4. 核心模块使用：事件通知走 event_bus（EVENT_BUS_PUBLISH/subscribe）；
   周期/延时工作走 tasker（组件内禁建 FreeRTOS 任务）；日志统一
   logger.h 分级 LOGD/I/W/E（TAG="[组件名]"）；硬件参数从设备树
   DTREE_* 读取，禁止硬编码引脚/地址/速率；模块接入走 app_init
   注册表+holder 编排（optional 模块缺席自动降级）；文件走 ovs_vfs；
   大块内存走项目内存池，禁裸 pvPortMalloc。
5. 可移植性：组件公共头禁止 esp/FreeRTOS 平台头；平台能力经依赖
   注入（deps 函数指针表）；自检清单 SKILL.md §7.10。
6. 验证纪律：每次交付必须跑 PC 门禁 tests/ovs_tests（mock 依赖回环）
   + idf.py build，两者全绿才算完成；完成后按规范更新
   docs/development_log.md 顶部条目（目标/完成/验证/待办/变更）。
```

## 2. 音频方案（MAX98357A + 麦克风，仿 Linux 音频栈）

### 2.1 需求

R1 播放系统提示音（收消息"叮"、按键音）；R2 播放 W25Q128 上的语音留言
（v1 PCM，v2 挂 codec2 解码）；R3 音量分档+静音（重启保持）；R4 异步播放
（调用不阻塞，完成/失败发事件）；R5（预留）实时语音流写入。
麦克风录音通路本期只保证不被回归破坏（语音留言录制归 lora 批次）。

### 2.2 分层（对应 Linux 声卡栈的简化映射）

```
应用/UI            audio_player_play(file/preset)，订阅 EVENT_AUDIO_*
─────────────────────────────────────────────────────────
audio_player(新)   播放器策略层（≈userspace player）：播放队列、
 modules/          文件头解析、解码器挂点（v1 passthrough，v2 codec2
 audio_player      函数指针注入）、音量策略持久化(NVS)
─────────────────────────────────────────────────────────
audio_module(演进) PCM 设备抽象（≈ALSA pcm core）：异步写（环形缓冲
 modules/          + tasker 喂数据）、软件音量缩放、静音(SD引脚经
 audio_module      gpio 驱动)、drain/stop、完成回调
─────────────────────────────────────────────────────────
i2s_drv(现有)      总线驱动（≈sound card driver）：DMA 读写，不改
```

### 2.3 关键决策

- **演进而非重写**：audio_module.h 现有 init/deinit/录音 API 保持兼容，
  新增 `audio_module_write_async()/set_volume()/mute()`，play() 阻塞版
  保留一个版本周期后删除；
- 音量 = 软件 PCM 缩放（int16 饱和乘法）+ SD 引脚硬静音两级；MAX98357A
  GAIN 引脚硬件固定（当前 15dB），不进软件控制；
- 事件：`EVENT_AUDIO_PLAY_STARTED/PLAY_DONE/PLAY_FAILED`（载荷含
  player_token 与错误码）；进度事件节流 10%；
- 设备树：沿用 i2s 节点，audio_module 节点补 `sd_pin/volume_default/
  buf_frames` 键；
- 解码挂点：`audio_player` 定义 `audio_decoder_t`（probe/decode 函数指针
  + 魔数匹配），codec2 后续以独立小组件接入，不改动 player 主体。

### 2.4 验收

1. ovs_tests：mock i2s 驱动下，异步播放 10KB PCM→PLAY_DONE 事件、
   字节序/音量 0/50/100 缩放正确性、播放中 stop→drain 语义、
   队列 3 条依次播放；
2. 板端（真机批次）：外放可闻提示音与 8kHz 语音样例，音量 5 档可辨。

## 3. AHT30 方案（审计收口，非重写）

### 3.1 需求

R1 周期采集（默认 30s，设备树可配）；R2 event_bus 广播（UI/逻辑订阅）；
R3 传感器缺席/ CRC 错误降级（重试 N 次后发 ERROR 事件，不阻塞系统）；
R4 PC 单测覆盖（mock i2c）。

### 3.2 方案要点

现有实现已含 init/采集/event_bus 发布，按清单收口：
1. 确认事件类型已在 event_bus_types.h 正式注册（MODULE_ID_AHT30 +
   EVENT_AHT30_DATA/ERROR，载荷 struct 版本化）；
2. 确认采集走 tasker 周期任务（Middle 级），间隔从设备树 aht30 节点
   `sample_interval_ms` 读取；
3. 数据结构审计：温度/湿度定点或浮点统一（建议 milli 度 int32，
   避免板端软浮点）；CRC 校验（AHT30 状态字节+CRC8）是否已实现；
4. tests/ovs_tests 增加 mock i2c 单测（正常/超时/CRC 错误三例）；
5. docs 命名已统一（本次完成），代码无需改。

## 4. LVGL UI 方案（六层实装）

### 4.1 需求

R1 主页：时间（无 SNTP 时 `--:--`）、温湿度卡片、LoRa 状态角标（信号/
邻居数/未读数）；R2 LoRa 会话页：邻居设备列表 → 聊天页（文本气泡+
语音条+按住录音按钮+播放条）；R3 设置页：音量/设备名/LoRa 档位；
R4 全程触摸可用，页面切换 ≤300ms，无阻塞卡顿（业务在 bridge 侧异步）。

### 4.2 实装顺序与铁律

```
里程碑 M1（框架跑通）：themes/base 主题 → ui/controls+indicators 基础
  控件（卡片/列表项/气泡/圆形按钮）→ navigator 页面栈 → pages/home
  静态版 → bridge 假数据注入。验收：PC 模拟器+板端均可导航三页。
里程碑 M2（真数据）：bridge 订阅 EVENT_AHT30_DATA/EVENT_AUDIO_*/LoRa
  状态刷新主页；设置页读写音量/设备名。
里程碑 M3（聊天）：lora/pages 聊天页接 EVENT_LORA_TP_*，录音按钮走
  audio_module 采集 + lora_tp 发送（依赖 lora 批次完成）。
```

铁律：ui/widgets/pages 三层**零业务头文件**（只 include lvgl.h 与层内
头）；业务依赖只出现在 presenters/bridge；bridge 是全工程唯一同时
include lora_tp.h/audio_player.h/aht30.h 的地方（对 UI 屏蔽后端，
接口为桥接函数表）；刷新一律事件驱动（bridge 订阅 event_bus 后投递到
LVGL 线程锁内更新），pages 内禁止轮询。

### 4.3 验收

M1：三页导航+返回、主题明暗两套、控件焦点可用；ovs_tests 增 navigator
页面栈逻辑单测（PC 可跑部分）；idf.py build + PC 模拟 build 双绿。
M2：主页数值随传感器事件变化（PC 用注入事件验证）；M3 与 lora 批次
联调（另立验收）。

## 5. 批次安排（2026-09-09 起并入三任务并行体系）

> 原批次已全部分配进 docs/three_tasks_plan.md 的三个并行任务，去向见其 §8
> 对照表：T1→[HUB] 子批次②、T2→[GUI] 子批次①、T3→废弃（[GUI] 子批次②
> 取代）、T4→[HUB] 子批次③、T5→集成阶段。本表保留原规模评估供参考。

| 原批次 | 内容 | 规模 | 现归属 |
|---|---|---|---|
| T0 ✅ | ATH30→AHT30 文档命名统一 | 已完成 | — |
| T1 | AHT30 审计收口+单测 | 小 | [HUB] 子批次② |
| T2 | audio_module 异步化+audio_player | 中 | [GUI] 子批次① |
| T3 | UI 里程碑 M1（框架+主页静态） | 中 | 废弃→[GUI] 子批次② |
| T4 | lora_tp 传输层 | 大 | [HUB] 子批次③ |
| T5 | UI M2/M3 + 真机联调 | 中 | 集成阶段（task_board 协调） |

## 6. 实现提示词

### 6.1 T1 AHT30 审计收口

```
任务：审计并收口 OVS 的 AHT30 温湿度模块（勿重写，已有实现可用）。

必读：.agents/skills/esp32s3-smart-assistant/SKILL.md；
components/modules/aht30/{aht30.h,aht30.c}；components/core/event_bus/
event_bus_types.h（LORA/AHT 相关事件段）；components/dtbs/config/
ovs.dtb.json 的 aht30 节点；docs/idle_modules_plan.md §3。

【设计原则】（本任务全程遵守）
（粘贴 §1 通用设计原则全文）

工作项：
1. 事件规格：确认/补全 event_bus_types.h 中 MODULE_ID_AHT30 的事件
   EVENT_AHT30_DATA/EVENT_AHT30_ERROR 及载荷结构（温湿度用 int32
   milli 单位，注释注明单位），aht30.c 发布点与之一致；
2. 周期采集：确认采集经 tasker 周期任务（Middle 级），间隔读设备树
   sample_interval_ms（无该键默认 30000，LOGW 提示）；
3. 健壮性：确认实现 AHT30 CRC8 校验；连续失败 3 次→发 ERROR 事件并
   停止采样，之后每 5 次周期重试一次恢复（LOGW 记录）；
4. 单测：tests/ovs_tests 新增 aht30 用例，mock i2c_drv 覆盖三场景：
   正常读取、设备无应答（超时）、CRC 错误；发布事件断言载荷正确；
5. 禁止改动 aht30.h 现有函数签名；新增接口须经设计评审说明。

验收：ovs_tests 全绿 + idf.py build 通过；更新 development_log.md。
```

### 6.2 T2 音频输出升级（MAX98357A）

```
任务：将 OVS 音频通路升级为异步播放服务（演进 audio_module + 新增
audio_player），勿重写录音通路。

必读：.agents/skills/esp32s3-smart-assistant/SKILL.md；
components/modules/audio_module/{audio_module.h,audio_module.c}；
components/drivers/i2s_drv/i2s_drv.h；components/drivers/gpio/gpio_ctrl.h
（SD 静音脚）；components/dtbs/config/ovs.dtb.json i2s/audio 节点；
docs/idle_modules_plan.md §2；docs/development_log.md 顶部（未提交上下文）。

【设计原则】（本任务全程遵守）
（粘贴 §1 通用设计原则全文）

工作项（分层边界严格按 idle_modules_plan §2.2）：
1. audio_module 演进：新增 audio_module_write_async()（环形缓冲
   + tasker Little 级喂数据）、set_volume(0~100)/get_volume、mute(bool)
   （SD 引脚经 gpio 驱动控制）、drain/abort；播放完成回调+事件；
   现有阻塞 play() 保留并标注 deprecated；软件音量为 int16 饱和缩放；
2. 新增 components/modules/audio_player/：播放队列（深度≥3）、文件
   播放（ovs_vfs，裸 PCM 头 audio_player_file_hdr_t：magic/.rate/ch/
   bits/len）、audio_decoder_t 解码挂点（v1 注册 passthrough 一个）、
   音量持久化 NVS；API：audio_player_play_file/play_mem/stop/set_volume
   /get_volume，token 查询状态；
3. 事件：event_bus_types.h 增加 EVENT_AUDIO_PLAY_STARTED/DONE/FAILED
   （载荷 token+err+duration_ms），进度事件节流 ≥10%；
4. 设备树：audio 节点补 sd_pin/volume_default/queue_depth 键，DTREE_
   读取+缺省兜底；
5. 单测：mock i2s 驱动覆盖 idle_modules_plan §2.4 第 1 条全部场景。

验收：ovs_tests 全绿 + idf.py build 通过；更新 development_log.md。
真机试听归 T5 批次，本次不做。
```

### 6.3 T3 UI 框架（里程碑 M1）

> **已废弃，由 docs/three_tasks_plan.md §6 的 [GUI] 任务提示词取代**
> （2026-09-09：需求升级为蓝白主题/`_(label)` 多语言/时间与网络信息/
> 待机画面接口，并纳入三任务并行体系 [HUB]/[GUI]/[WEB]）。

```
任务：实装 OVS LVGL 六层架构至里程碑 M1（框架跑通+主页静态版），
假数据驱动，不接任何业务模块。

必读：.agents/skills/esp32s3-smart-assistant/SKILL.md；
src/lvgl/README.md（六层职责与目标结构）；src/lvgl/lvgl_app.c 与
port/（现有启动与显示口）；docs/idle_modules_plan.md §4；
docs/development_log.md 顶部（未提交上下文，PC 模拟与板端分开的既有
机制勿破坏）。

【设计原则】（本任务全程遵守，另加 UI 铁律）
（粘贴 §1 通用设计原则全文）
【UI 铁律】ui/widgets/pages 三层零业务头文件（只 include lvgl.h 与
层内头）；业务依赖只允许出现在 presenters/bridge；pages 内禁止轮询
与阻塞调用，刷新一律经 bridge→presenters 投递。

工作项：
1. themes/base：明暗两套主题（主色/卡片/文本三级样式，style 常量
   集中定义）；
2. ui/controls+indicators：card（图标+主值+副值）、list_item、
   chat_bubble（左/右+文本）、round_btn（按住语义）、status_dot；
   全部仅回调出参，无业务；
3. navigator：页面注册表+push/pop/switch（带 300ms 滑入动画）、
   页面栈深度限制、on_enter/on_exit 生命周期；
4. pages：home（时间 00:00、温湿度卡片、LoRa 状态角标、未读角标）、
   settings（音量 slider、设备名、占位项）、lora（会话列表空态页）
   ——三页全部假数据；
5. presenters+bridge：bridge 定义注入接口（struct 前向声明的函数
   表：get_sensor_data/get_lora_status/...），M1 用 mock 实现；
   presenters 持有页面并绑定回调；
6. navigator 栈逻辑进 tests/ovs_tests（PC 可跑，不依赖 lvgl 渲染的
   部分抽纯逻辑测试）。

验收：PC 模拟与 idf.py build 双绿（板端真机显示验证归 T5）；三页
可导航返回、主题可切、代码层依赖经 grep 检查无违规 include；
更新 development_log.md。
```

### 6.4 T4 lora_tp

见 `docs/lora_transport_design.md` §9（提示词自包含，含必读/约束/
验收/日志要求），前置条件为 DX-LR22 手册核实清单（lora_protocol.md §10）。
