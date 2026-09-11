# OVS 重构与架构优化方案

| 项目 | 内容 |
|------|------|
| 文档版本 | v1.1（2026-09-09 修订，变更记录见附录 D） |
| 日期 | 2026-09-09 |
| 适用代码基线 | main 分支 @ 0889f2b6（其后三次提交：VFS 修复 / 设备树 A/B OTA / WiFi 热切换均已合入） |
| 目标读者 | 本项目后续所有开发会话（人/AI 均适用），是下阶段开发的**总纲** |
| 关联文档 | `docs/ARCHITECTURE.md`（现行架构）、`docs/development_log.md`（进度）、`docs/diy-smart-assistant/`（产品需求） |

> 本文档基于对全部源码、构建配置、脚本与文档的逐文件审读编写，所有"现状"描述均以当前代码为准，所有"过时"判定均给出可验证依据。

---

## 目录

- [一、项目现状总体评估](#一项目现状总体评估)
- [二、过时与无效内容判定](#二过时与无效内容判定)
- [三、目标功能与差距分析](#三目标功能与差距分析)
- [四、目标架构总体设计](#四目标架构总体设计)
- [五、核心服务层重构设计](#五核心服务层重构设计)
- [六、显示与 UI 子系统设计](#六显示与-ui-子系统设计)
- [七、音频子系统设计](#七音频子系统设计)
- [八、LoRa 对讲子系统设计](#八lora-对讲子系统设计)
- [九、网络与蓝牙配网设计](#九网络与蓝牙配网设计)
- [十、Web 上位机扩展设计](#十web-上位机扩展设计)
- [十一、时间服务设计（时钟/闹钟/定时器）](#十一时间服务设计时钟闹钟定时器)
- [十二、电源管理设计](#十二电源管理设计)
- [十三、模块间协作：关键场景时序](#十三模块间协作关键场景时序)
- [十四、性能优化专项](#十四性能优化专项)
- [十五、实施路线图](#十五实施路线图)
- [十六、风险与关键决策点](#十六风险与关键决策点)
- [附录 A：过时内容处置总表](#附录-a过时内容处置总表)
- [附录 B：新增事件字典](#附录-b新增事件字典)
- [附录 C：新增组件与文件清单](#附录-c新增组件与文件清单)

---

## 一、项目现状总体评估

### 1.1 已验证可用的主线（保留，作为重构地基）

以下链路在真机上验证通过（开发日志 2026-09-08/09 有实测记录），**重构不推翻，只做加固**：

| 能力 | 承载组件 | 验证状态 |
|------|----------|----------|
| WiFi STA/AP 管理、免重启热切换、凭据三级优先级（显式>设备树哈希>NVS） | `modules/net_mgr` + `drivers/wifi` | 真机双向切换 PASS |
| OTA：流式直写 + SHA256 + 15s 回滚确认 + `OVSO` 容器（app+设备树配对试运行） | `modules/ota` + `web/web_ota.c` + `dtbs/dtb_ab` | 真机 5 项测试 PASS |
| 设备树：单棵 JSON 树 + compatible 绑定 + A/B 裸分区槽 | `dtbs/dtree` + `dtbs/dtb_ab` | 真机验证 |
| 外部存储：W25Q128 驱动（健康状态机）+ ovs_vfs 三层抽象 + littlefs 挂载 | `modules/w25q128` + `core/ovs_vfs` | 压测 54/54 PASS |
| Web：Mongoose 7.21 单任务事件驱动、流式上传通道、WS 推送、网页热更新 | `modules/web` | 在用 |
| Vue 前端：仪表盘 / WiFi 配网（含 30s 回退倒计时）/ OTA 三个页面 | `web/vue-ui` | 在用 |
| 共享总线驱动：SPI/I2C/I2S/UART 引用计数 + 设备树配置 | `drivers/*` | SPI 链路真机验证，其余编译级验证 |

### 1.2 "写完未接线"的模块（盘活对象，不是重写对象）

`st7789`、`cst816s`、`ath30`、`lora`、`audio_module`、`holder` 共约 4600 行代码**已编译进固件但运行时无人调用**（main.c 未初始化）。它们的共同特点：

- 设备树绑定（find_by_compatible → 总线驱动）已完成，模式统一；
- 驱动层质量参差：ath30 完整（含 CRC8），st7789/cst816s 可用但**签名与 LVGL 不兼容**，lora **协议层为零**，audio_module 是骨架；
- 缺的是：main.c 接线 + 上层消费者（LVGL/音频管线/对讲业务）。

**结论：这批模块是"半成品资产"，重构方式是补齐+接线，而非删除重写。**

### 1.3 核心判断

1. **架构方向正确**："Linux 式"分层（设备树配置 → 总线驱动引用计数 → 模块策略 → 事件解耦）已在主线验证有效，保留。
2. **三大系统性缺陷**：
   - **include/ 全局头目录**与组件头同名不同体（logger/ota/web/heartbeat/tasker/dtree 共 6 对），靠顶层 CMake 全局注入苟活，是最危险的隐患；
   - **event_bus 零订阅者**：43 个事件全部发往虚空，事件驱动只是"半个事实"；
   - **初始化编排缺失**：main.c 手工硬编码顺序 vs holder 依赖拓扑编排器零调用，两套并存取其一。
3. **最大空白是 UI**：LVGL 库本身不在项目里（`include/lvgl.h` 是 24 行 stub），`src/lvgl` 六层目录只有 README；而产品目标 12 项功能里 9 项依赖 UI。
4. **分区表有结构性风险**：ota 分区 2.5MB，而 web_data.c 内嵌网页资产已 706KB，再叠加 LVGL/蓝牙/JPEG 解码器必爆；同时内部 littlefs（/config）8.75MB 对配置数据严重超配——**分区需要重排**（见 14.5）。

---

## 二、过时与无效内容判定

### 2.1 判定原则

- **代码**：以"是否参与编译 / 是否被运行时调用 / API 与实现是否一致"三个维度判定；
- **文档**：以"与当前代码/设备树/分区表是否可证伪地冲突"判定（目标愿景类文档除外）；
- **历史记录**（开发日志、ARCHITECTURE 更新日志段）不回写，保留历史原貌。

### 2.2 代码类过时/无效内容

| # | 位置 | 问题 | 判定依据 | 处置 |
|---|------|------|----------|------|
| C1 | `include/logger.h` | 与组件版 `core/logger/logger.h` 同 guard 同名不同体（DEBUG 恒开、LOGD 语义不同），全局注入导致不同组件可能解析到不同版本 | 两文件 diff | **删除**，见 5.3 |
| C2 | `include/ota.h`、`web.h`、`heartbeat.h`、`tasker.h`、`event_bus.h` | 与组件头同名的旧副本，内容不同步 | 逐对 diff | **删除**，头文件回归组件 |
| C3 | `include/dtree.h` + `dtree.h.bak` | 真实使用的 dtree API 头却放在全局目录，组件 INCLUDE_DIRS 未自含 | dtbs CMake 无该头 | **移入** `components/dtbs/`，删 .bak |
| C4 | `include/lvgl.h` | 24 行假 LVGL stub（空 `lv_init()`），掩盖"LVGL 未引入"事实 | 全文 24 行 | **删除**（引入真 LVGL 后无意义） |
| C5 | `components/api/eventbus_api/` | 空壳：eb.c 1 行有效代码、eb.h 空 guard，无人 include | grep 零引用 | **删除**整个组件 |
| C6 | `include/tasker.h` 与 `api/tasker_api/tasker.h` | 同一 API 两份头（注释语言不同）；5 个模块 CMake REQUIRES `tasker` 却 include api 的头 | 各模块 CMake vs 源码 include | 统一到 `api/tasker_api`，见 5.2 |
| C7 | `thirdparty/littlefs-2.11.3/`（1.3万+行） | 纯源码拷贝，无任何构建引用（esp_littlefs 自带 littlefs） | 根 CMake/组件 CMake 无引用 | **删除** |
| C8 | `core/event_bus/event_bus_test.c`、`example_usage.c`、`core/tasker/tasker_test.c` | 测试代码编译进产品固件但无人调用，白占 flash | grep 零调用 | 移入独立 `tests/` 组件或加 `OVS_ENABLE_TESTS` 开关排除 |
| C9 | `modules/*/storage_module.c`、`display_module.c`、`wireless_module.c`、`touch_module.c`、`sensor_module.c` | 空壳/单行注释文件 | wc -l | **删除** |
| C10 | `main/main.c.bak`、`main.c.backup`、`CMakeLists.txt.bak`、`dtree.h.bak`、`docs/diy-smart-assistant/**.bak` ×6 | 历史备份残留 | 文件名 | **删除**（git 即备份） |
| C11 | `drivers/gpio/`、`drivers/led/` | 完整但零调用；上层模块实际直接用 `driver/gpio.h`；LED 无消费者（dtree `leds.status` 节点无人读） | grep 零调用 | **删除**两个驱动；状态灯并入 power_srv（12.4） |
| C12 | `modules/heartbeat/` | `heartbeat_init()` 无人调用 → `s_cached_rssi` 恒 0，`/api/status` 与 WS 推送的 rssi 字段**一直是错的**（真值在 `/api/wifi/status`） | grep + web.c:938 直接读缓存 | **修复接线**（Phase 0 一行修复）或并入 sys 状态服务 |
| C13 | `w25q128.h` 注释 | 声称 write "自动擦除"、错误码含 `W25Q128_ERR_VERIFY`，实现两者皆无 | 对照 w25q128.c | **改文档**防误用 |
| C14 | `w25q128_read()` | 每次调用 malloc tx/rx 临时缓冲（堆碎片+性能） | 源码 | 改为一次性分配的静态/heap_caps 缓冲（5.4 同类修复） |
| C15 | `cst816s_touch_cb_t`、`lora_config_t` | 定义了但全工程未使用（LoRa 模块参数配置代码缺失，见 8.2） | grep | cst816s 死类型删除；lora_config_t **补实现**（M0/M1 配置模式） |
| C16 | `scripts/sign_firmware.py` | "OVAO" 尾部签名，web_ota.c 不识别该格式，注释自认 OTA 无需 | web_ota.c 解析逻辑 | **删除**或明确仅烧录校验用途 |
| C17 | `web/vue-ui/src/components/HelloWorld.vue` | Vite 模板残留 | 未被引用 | **删除** |
| C18 | `ovs.dtb.json` 中 `wifi.sta.password` 明文 | 家用 WiFi 密码随固件/SPIFFS 镜像/dtb.bin 分发 | JSON 原文 | 开发期可容忍，发布版改为占位符+首启强制配网（9.4） |

### 2.3 文档类过时内容

| # | 文档 | 问题 | 处置 |
|---|------|------|------|
| D1 | `docs/diy-smart-assistant/docs/hardware_notes_for_software.md`（v3.3 引脚表） | **与接线图/设备树大面积冲突**：该文档称 LoRa M0=GPIO1/M1=GPIO2/AUX=GPIO37/TXD=36/RXD=35、LCD_RST=GPIO39、LED=GPIO14、I2S BCLK=5/WS=6；而 `wiring_diagram.md` 与 `ovs.dtb.json`（真机验证）为 M0=8/M1=3/AUX=46/TXD=9/RXD=10、触摸 RST=39、LCD RST=21、LED=4、BCLK=6/WS=5。该文档是旧版接线 | **重写引脚表**（以 dtree 为唯一权威源），表头标注版本 |
| D2 | `docs/ARCHITECTURE.md` §2.2 | Tasker"三级优先级"描述理想化：实现中 priority 维度恒为 last、按 level 路由、超时不能中断任务 | 随 5.2 重构同步修订 |
| D3 | `docs/ARCHITECTURE.md` §2.5 Holder | 描述为现行机制，实际零调用 | holder 启用后（4.4）修订，否则标注"未接线" |
| D4 | `docs/PROJECT_STRUCTURE.md`、各组件 README | 2026-09-09 刚做过一致性校准，**现行有效**；但 `drivers/gpio`、`led`、`api/eventbus_api`、`thirdparty/littlefs-2.11.3` 若删除需同步 | 随 Phase 0 清理同步 |
| D5 | `docs/diy-smart-assistant/docs/development_roadmap.md` | 4 周路线图基于"从零写驱动"的假设，现状已完成大半，任务粒度失效 | 由本文档十五节路线图**取代**（原文档标注 superseded） |
| D6 | `docs/diy-smart-assistant/docs/lora_protocol.md` | 帧格式是初稿：48 字节音频帧 + PCM/ADPCM 编码假设，与 LoRa 空口带宽现实冲突（见 8.5）；帧结构无同步头/CRC 位置错误 | 按第八节修订 v2 |
| D7 | `wiring_diagram.md` LED 引脚 | 表内自相矛盾：L20 说 GPIO4，L381 汇总表说 GPIO14 | 与实物核对后统一（dtree 现值 4） |

### 2.4 构建配置类问题

| # | 配置 | 现值 | 问题 | 处置 |
|---|------|------|------|------|
| B1 | `CONFIG_COMPILER_OPTIMIZATION_DEBUG` | Debug | 发布前必须切 SIZE/-O2，flash 占用差 20-40% | 发布配置 `sdkconfig.release`（15.6） |
| B2 | `CONFIG_FLASHMODE_DIO` | DIO @80MHz | 16MB flash（W25Q 系）普遍支持 QIO，XIP 取指/字体读取带宽翻倍 | Phase 2 实测 QIO 稳定性后切换 |
| B3 | `CONFIG_SPIRAM_SPEED_40M` | 40MHz | S3 八线 PSRAM 支持 80MHz，JPEG 解码/大缓冲吞吐翻倍 | Phase 2 实测后切换 |
| B4 | BT/NimBLE | 未启用 | 蓝牙配网目标需要 | Phase 5 启用 NimBLE + coexist（9.3） |
| B5 | ota_0/ota_1 = 2.5MB | — | web_data 706KB + LVGL + 蓝牙 + JPEG 解码器逼近上限 | 分区重排（14.5），ota 扩到 5.5MB |
| B6 | `CONFIG_LWIP_TCP_SND_BUF_DEFAULT` 5760 等 | 默认 | OTA 上传实测 ~700KB/s 有提升空间 | 14.4 调优 |

---

## 三、目标功能与差距分析

产品目标（用户定义 + `docs/diy-smart-assistant/`）：LVGL 触屏 UI 系统、LoRa 远程对讲、Web 上位机、蓝牙配网四大板块。逐项对照：

| 目标功能 | 现状 | 差距 | 依赖的新基建 |
|----------|------|------|--------------|
| 图片轮换显示 | 无（无 LVGL、无解码器） | LVGL 引入、JPEG/PNG 解码、相册数据源 | LVGL、esp_jpeg、/media |
| 视频播放 | 无 | MJPEG 解码 + 定时刷帧 +（后期）音轨 | esp_jpeg（S3 硬件 JPEG 解码器）、time_srv |
| WiFi 连接 UI | 后端 API 齐全（scan/connect/mode） | 纯 UI 层工作 + 键盘控件 | LVGL 六层 |
| 蓝牙连接 UI（配网入口） | 无蓝牙栈 | NimBLE + 配网流程页 | wifi_prov_mgr（9.2） |
| 闹钟 / 定时器 | 无 | 时间服务全新模块 | time_srv（十一）、SNTP |
| 待机动画 | 无 | 低帧动画 + 背光策略 | power_srv（十二） |
| 语音录制 | audio_module 骨架（事件带裸指针） | 录音管线重写 + 文件落盘 + UI | audio_srv（七） |
| 对讲机 UI | 无 | PTT 会话页 + 状态联动 | lora_proto（八）+ audio_srv |
| 音视频播放 UI | 无 | 播放器页（进度/暂停） | media_player（6.5） |
| 滑动导航切换页面 | 无 | tileview + 页面注册表 | LVGL（6.4） |
| LoRa 远程对讲 | lora.c 仅 UART 透传收发，模块参数配置代码缺失 | 协议栈、编解码、会话、留言 | Codec2、lora_proto（八） |
| Web 文件传输 | 无（OTA 流式通道可复用） | 文件管理 API + 前端页 | web 扩展（十） |
| Web 闹钟设置 / 远程播放语音 / 远程播放音视频 | 无 | API + 与本地服务联动 | time_srv、audio_srv、media_player |
| 蓝牙配网 | net_provision 接口已预留（web provider 已接） | BLE provider | wifi_prov_mgr（九） |
| 温湿度显示（附带） | ath30 驱动完整未接线 | 一行 init + UI 挂件 | — |

**依赖拓扑结论**：几乎所有差距收敛到五个新基建——**LVGL 运行时、音频管线服务、LoRa 协议栈、时间服务、电源管理**。这五个基建 + 核心层加固构成下文设计主体。

---

## 四、目标架构总体设计

### 4.1 分层架构总图（重构后）

```
┌────────────────────────────────────────────────────────────────────────┐
│ 应用层 (src/app + src/lvgl)                                            │
│  app_main 启动编排(holder)   LVGL UI 应用(六层)   对讲业务(intercom)   │
├────────────────────────────────────────────────────────────────────────┤
│ 服务层 (modules，业务策略)                                             │
│  audio_srv 音频管线 │ media_player 媒体播放 │ time_srv 时钟/闹钟       │
│  power_srv 电源/背光 │ lora_proto 对讲协议 │ ble_prov 蓝牙配网        │
│  net_mgr 网络 │ ota 升级 │ web 上位机(HTTP/WS/文件管理)               │
├────────────────────────────────────────────────────────────────────────┤
│ 设备模块层 (modules，单设备策略)                                       │
│  st7789 显示 │ cst816s 触摸 │ w25q128 存储 │ ath30 传感 │ lora 无线   │
├────────────────────────────────────────────────────────────────────────┤
│ 核心服务层 (core)                                                      │
│  event_bus(修复) │ tasker(重构) │ logger(esp_log封装) │ ovs_vfs(加锁) │
├────────────────────────────────────────────────────────────────────────┤
│ 总线驱动层 (drivers，引用计数共享)                                     │
│  spi_drv │ i2c_drv │ i2s_drv │ uart_drv │ wifi                        │
├────────────────────────────────────────────────────────────────────────┤
│ 设备树层 (dtbs)                                                        │
│  dtree 解析器 + dtb_ab A/B槽 │ ovs.dtb.json（唯一硬件权威源）          │
└────────────────────────────────────────────────────────────────────────┘
        横切：holder 模块注册表（初始化编排/状态机/降级）
              event_bus（模块间唯一解耦通道，修复后全链路使用）
```

与现状的差异：
1. 删除 `drivers/gpio`、`drivers/led`、`api/eventbus_api`；
2. modules 新增 6 个服务型模块（audio_srv / media_player / time_srv / power_srv / lora_proto / ble_prov）；
3. holder 从死代码变为**初始化编排中枢**；
4. include/ 全局目录解散。

### 4.2 运行时任务模型（目标态）

双核分工原则：**Core0 = 通信与系统（WiFi/BT/lwIP/web/event_bus 分发），Core1 = 交互与媒体（LVGL/音频/JPEG）**。

| 任务 | 归属 | 栈 | 优先级 | 核 | 说明 |
|------|------|-----|--------|-----|------|
| WiFi/BT/lwIP 系统任务 | IDF | 系统默认 | 系统 | 0 | coex 开启后共享天线 |
| `event_bus` 分发任务 | core/event_bus | 4KB | 5 | 0 | 现有，保持 tskNO_AFFINITY 亦可 |
| tasker worker ×3 | core/tasker | 3/4/8KB | 3/3/2 | Any | 重构后空闲阻塞不轮询（5.2） |
| `web_task` | modules/web | 8KB | 3 | 0 | Mongoose 轮询 50ms，现有 |
| `lvgl_task` | src/lvgl | 16KB | 4 | **1** | `lv_timer_handler()` 5ms 周期 + 显示锁 |
| `audio_in`（I2S→队列） | audio_srv | 4KB | 5 | **1** | 录音采集，psram 缓冲 |
| `audio_out`（队列→I2S） | audio_srv | 4KB | 5 | **1** | 播放，含混音 |
| `jpeg_task` | media_player | 12KB | 3 | **1** | 视频帧解码（软/硬解） |
| `lora_rx` | lora 模块 | 4KB | 4 | Any | UART 事件驱动收包（重写为事件驱动，8.2） |
| `intercom` 会话任务 | lora_proto | 6KB | 4 | Any | PTT 会话状态机 |
| idle/tasker/timer | IDF | — | 0/1 | — | 系统任务 |

> 原则：周期轮询任务（ath30 1s、cst816s 现为 20ms 轮询）中，**触摸改中断驱动**（6.3），ath30 等慢传感器留在 tasker。

> **v1.1 分工终版裁决**：自建任务仅限上表（lvgl / audio_in / audio_out / codec / jpeg / touch / lora_rx，web 既有）；**无自有任务、纯事件驱动**：net_mgr、power_srv（event_bus+esp_timer）、lora_proto 会话状态机、intercom 业务逻辑、ble_prov（NimBLE 栈自带任务）；**进 tasker**：ath30 轮询、time_srv 闹钟比对、heartbeat 采样、lora 信标广播触发；20ms 级背光渐变步进用 esp_timer 不进 tasker。口诀：**持续循环或硬实时→自建任务；间歇秒级短活→tasker；纯事件响应→event_bus 订阅+esp_timer**。

### 4.3 事件驱动模型

**决策：保留并修复 event_bus，作为模块间唯一解耦通道；全部跨模块通知走事件，禁止模块间头文件互相 include 业务 API（服务注册接口除外）。**

修复后的接线承诺（当前为零订阅者，重构后必须有真实订阅）：

| 事件域 | 发布者 | 订阅者 | 用途 |
|--------|--------|--------|------|
| WIFI_*（现有 8 个） | net_mgr | bridge_wifi（UI 状态栏）、web（WS 推送）、time_srv（GOT_IP→SNTP） | 网络状态全端同步 |
| TOUCH_* | cst816s 中断任务 | （改走 LVGL indev，事件仅保留手势 GESTURE） | 手势唤醒/快捷操作 |
| SENSOR_DATA | ath30 | bridge_sensor → UI 挂件、web /api/status | 温湿度 |
| LORA_* / INTERCOM_* | lora_proto | intercom presenter（UI）、web（WS） | 对讲呼入/留言 |
| AUDIO_* | audio_srv | UI 播放器页、web | 录放状态 |
| MEDIA_* | media_player | UI 播放器页 | 进度/完成 |
| TIME_*（新） | time_srv | UI 闹钟页 + audio_srv（响铃）+ web | 闹钟/定时器触发 |
| POWER_*（新） | power_srv | UI（进/出待机动画）、audio_srv、web | 电源状态迁移 |
| STORAGE_ERROR/READY（现有） | w25q128 | UI toast、web | 存储健康 |

完整新增事件字典见附录 B。事件内存改为池化（5.1）。

### 4.4 初始化编排：启用 holder 替代手工 main.c

**决策：main.c 的 13 步手工序列收敛为 holder 依赖拓扑编排**（holder 已实现依赖分批初始化、循环检测、必需/可选降级，正是为此而写）。

目标注册表（`src/app/app_init.c`）：

```
holder_init()
├─ [批0 核心] logger → event_bus → tasker → dtree（required）
├─ [批1 存储] w25q128（dep: dtree, required）
│             → ovs_vfs 挂载（dep: w25q128, required）
├─ [批2 网络] net_mgr（dep: dtree, required）
│             → ota（dep: net_mgr, required）
│             → web（dep: net_mgr+ota, optional——失败仅失去远程管理）
├─ [批3 人机] st7789（dep: dtree, optional——失败仍可 Web 管理）
│             → cst816s（dep: dtree, optional）
│             → ath30（dep: dtree, optional）
│             → audio_srv（dep: dtree, optional）
├─ [批4 服务] time_srv（dep: net_mgr, optional）
│             → power_srv（dep: st7789, optional）
│             → lora_proto（dep: dtree, optional）
├─ [批5 应用] lvgl_app（dep: st7789+cst816s, optional——无屏不启动 UI）
│             → intercom（dep: lora_proto+audio_srv, optional）
└─ holder_init_all(stop_on_required_error=true)
```

规则：
- NVS/SPIFFS 挂载仍留在 app_main 手工第一步（holder 自身依赖它）；
- **required 仅 5 个**：核心 4 件 + 存储。网络类失败降级为"AP 模式重试"，人机类失败降级为"无屏运行"（Web 仍可用），这正是产品需要的容错形态；
- `holder_get_module_state()` 结果接入 `/api/status` 与 UI 开机页（每模块 init 耗时/状态一览）；
- main.c 现有 `OVS_ENABLE_NET` OTA 保护区注释保留，net_mgr/ota/web 的注册封装在 `net_stack_init()` 内部不变。

### 4.5 目标目录结构（增量）

```
components/
├── core/          event_bus / tasker / logger / ovs_vfs（重构，无新增目录）
├── drivers/       spi_drv / i2c_drv / i2s_drv / uart_drv / wifi（删 gpio、led）
├── modules/
│   ├── st7789/ cst816s/ w25q128/ internal_flash/ ath30/ net_mgr/ ota/ web/ heartbeat/（既有）
│   ├── audio_srv/      新：音频管线服务（七）
│   ├── media_player/   新：图片/视频/MJPEG 播放服务（6.5）
│   ├── time_srv/       新：SNTP 时钟/闹钟/定时器（十一）
│   ├── power_srv/      新：电源状态机/背光/状态灯（十二）
│   ├── lora_proto/     新：对讲协议栈（八，lora 驱动之上）
│   └── ble_prov/       新：蓝牙配网 provider（九）
├── dtbs/          dtree + dtb_ab（dtree.h 移入）
└── api/           tasker_api（保留；eventbus_api 删除）
src/
├── app/           main.c（瘦身为编排）+ app_init.c（holder 注册表）
└── lvgl/          六层 UI + port/（显示/触摸移植层，6.2/6.3）
thirdparty/        cJSON / mongoose / sha256 / codec2（新增）/ esp_littlefs / lvgl-9.5.0+lvgl_lib
sim/               PC 模拟器（SDL2 宿主，与 ESP 共编译 src/lvgl 六层；v1.1 新增）
web/vue-ui/        + FileManager.vue / Alarm.vue / Media.vue（十）
tests/             新：event_bus/tasker 测试迁移地（C8）
```

---

## 五、核心服务层重构设计

### 5.1 event_bus：修复四缺陷，然后真正用起来

现状：API 与数据结构合格，但 (a) `event_bus_dispatch_event()` 持锁调 handler（handler 慢→阻塞订阅/退订，handler 内再订阅=1s 锁超时假死）；(b) 每事件 malloc/free（高频触摸/音频事件堆碎片）；(c) 43 事件零订阅者；(d) 名表缺 8 个事件打印 UNKNOWN。

重构清单（改动集中在一个文件，风险低）：

1. **锁外回调**：dispatch 时在锁内把该事件的订阅者指针快照到栈上数组（上限 8，超出取堆），释放锁后逐个调用；
2. **事件内存池**：`static event_t pool[48]` + 使用位图（约 12KB 内部 RAM，换来零碎片、O(1) 分配）；池满退化为 malloc 兜底并计数告警；事件数据上限维持 256B——**音频流不过事件总线**（音频帧走 FreeRTOS 队列，见 7.2，事件只发元信息）；
3. **统计原子化**：dropped/published 用 portENTER_CRITICAL 或 atomic 加法；
4. **名表补全** + 新增事件域（附录 B）随之补表；
5. **API 不变**：`EVENT_BUS_PUBLISH/SUBSCRIBE` 宏、订阅句柄语义不动，所有现有调用方零修改。

验收：`event_bus_print_subscribers()` 在集成测试中列出 ≥6 个真实订阅者；压测 1000 事件/秒无堆增长（`heap_caps_get_free_size` 前后差 <2KB）。

**测试分层（v1.1）**：event_bus 纯 C 无硬件依赖，单元/压力测试在 PC 宿主跑（复用 sim/ CMake 工具链建 `ovs_tests` 目标）：订阅/退订生命周期、锁外回调（handler 内再订阅/退订不死锁）、池耗尽退化为 malloc 兜底、名表全覆盖非 UNKNOWN、快照上限 8 溢出堆扩展分支。PC 反复压测 + 真机最终验收双层。

### 5.2 tasker：原地重构（不推翻）

现状：能用但粗糙——dispatcher `vTaskDelay(1)` 忙轮询（双核空转）、标志位跨核无原子性、"优先级"维度名存实亡（恒 last）、超时只能升级不能中断。

**决策：保留 tasker_api 形态与三级 worker 分类（这个分类对慢周期任务是合理的），核心实现做三处修复：**

> **v1.1 追加约束（用户裁定）**：对外 API 与宏签名**冻结**，仅动内部实现；改动前后以 tests/ 的 PC 回归测试做门禁，防止影响现有调用方。

1. dispatcher 改**阻塞等待**：`xEventGroupWaitBits(s_wake_evt)`，enqueue 时 `set_bits`——空闲零 CPU；
2. `done/cancel/dispatched/timeout_flag` 读写全部进 `portENTER_CRITICAL` 短临界区；
3. 文档化两个既有约定（写进 tasker.h 注释与 ARCHITECTURE）：
   - **长任务（>1s）禁止进 tasker**，自建 FreeRTOS 任务（audio/web/lvgl 均如此）；
   - priority 维度废弃，仅 level 有效；
4. 顺带修 `worker_init()` 失败路径泄漏。

同时统一依赖：ath30/cst816s/lora/w25q128/audio_module 的 CMake `REQUIRES` 从 `tasker` 改为 `tasker_api`，`include/tasker.h` 副本删除（C2/C6）。

### 5.3 logger：保留 printf 实现（v1.1 改判，用户裁定）

现状：`printf` + ANSI 色码、等级过滤可用；无时间戳/任务名（放弃该收益）。存在同名异体旧头（C1，最危险项）。

**v1.1 改判：原"统一为 esp_log 薄封装"方案废弃。** 理由：PC 模拟器（sim/）当前零改造直接复用 `core/logger/logger.c`，绑定 esp_log 会拆掉双平台基建；时间戳/任务名为锦上添花非必需。

保留动作：仅删除 `include/logger.h` 同名异体旧副本（C1 不变——危险在双版本并存，不在实现本身）；运行期 `logger_set_level()` 维持。后期若确需 esp_log 后端（master level 编译期裁剪），按平台分层演进为 `logger_esp.c` + `logger_pc.c`，`logger.h` 接口不动。

### 5.4 ovs_vfs：加锁 + 清半成品

1. 挂载表/块设备表操作加一把互斥锁（当前仅启动期单线程调用是侥幸安全，Web 文件管理上线后必然多线程）；
2. 删除半成品 `vfs_mount()`（"LittleFS integration pending" 分支）与 `vfs_unmount()` 的 partition_label 误用分支，API 收敛为实际在用的 `vfs_mount_littlefs()` 族；
3. `ovs_vfs.h`/`dtree.h` 的 `#endif` 后悬挂声明移回 guard 内（现存编译侥幸）；
4. `MAX_PATH_LEN` 32→64，strncpy 截断加 LOGW；
5. w25q128 同类修复：`w25q128_read()` 的一次性 DMA 缓冲（C14）、头文件注释纠偏（C13）。

### 5.5 dtree：修四个坑，不动架构

dtree + dtb_ab 是全项目耦合最深、设计最完整的设施（A/B 槽事务机制是亮点），只修：

1. **节点池 16→48 槽**：环形覆盖在"同时持有 >16 句柄"时静默换目标（VFS 挂载循环 + UI 遍历时可能踩中）；同时在 `node_pool_alloc()` 覆盖未释放槽时 LOGW；
2. `dtree_get_array_item()` 返回 static 单例 → 改为 `dtree_array_get(array, idx, dtree_node_t* out)` 出参语义，原 API 标记 deprecated；
3. `dtree_get_node()` 的 strtok → 手写指针扫描（strtok 非线程安全且破坏 const）；
4. `dtree_get_parent()` O(n) 全树扫描 → dtree_node_t 内嵌 parent 指针（解析期构建，零运行时成本）。

### 5.6 include/ 解散（Phase 0 第一刀）

```
include/dtree.h      → components/dtbs/dtree.h（真实本体）
include/tasker.h     → 删（api/tasker_api/tasker.h 为准）
include/logger.h     → 删（5.3 后组件版唯一）
include/ota.h|web.h|heartbeat.h|event_bus.h|lvgl.h → 删
顶层 CMake idf_build_set_property(INCLUDE_DIRS include) → 删除
main/CMakeLists.txt  → REQUIRES 改为实际依赖组件
```

每删一个头跑一次全量构建，防止隐藏 include 路径依赖。

---

## 六、显示与 UI 子系统设计

### 6.1 LVGL 引入决策（v1.1：已实施，路线变更）

**已实施（2026-09-09）：LVGL v9.5.0 vendored + lv_conf.h 路线**，替代原"v9.2 managed component + Kconfig"方案：

- `thirdparty/lvgl-9.5.0`（裁非运行时目录）+ `thirdparty/lvgl_lib` IDF 包装组件（全量 glob src/*.c，bin_decoder 等必需解码器不可排除）；
- 配置走 `src/lvgl/lv_conf.h` 双平台共用一份（`OVS_SIMULATOR` 宏仅 PC 开 SDL），不使用 Kconfig；
- 内存 `LV_STDLIB_CLIB`（libc malloc，不用 64KB 内置静态池）；
- **PC 模拟器（sim/，SDL2）同步落地**——原方案没有的维度：UI 开发可全程脱离真机，六层 UI 两端编译同一份；
- R1（v9 × IDF 6.0.1）已真机编译+运行验证关闭；
- 实施细节：`docs/superpowers/specs/2026-09-09-lvgl-dual-platform-design.md`。

esp_jpeg（S3 硬解）仍按原计划 Phase 4 经 managed component 引入。中文外部字体 bin 放 /font 运行时加载（`lv_font_load`）——**前置依赖（v1.1 新识别）：需为 ovs_vfs 实现 lv_fs 后端**，i18n 多语言（附录 D）同样依赖它。

### 6.2 st7789 对接 LVGL（port/display_port.c）

现状缺口：`st7789_flush()` 是无参整帧刷，非 LVGL `flush_cb(disp, area, color_p)` 签名；宽高硬编码未读 dtree；framebuffer 非 DMA 内存。

设计：

```
lv_display_t* display_port_init(void)
├─ st7789_init()（改造：width/height/rotation/invert 从 dtree 读）
├─ draw_buf ×2：各 320×40×2B = 25.6KB，heap_caps 内部 DMA 内存（共 51KB；紧张可降 20 行×2=25.6KB，骨架现为 20 行）
├─ lv_display_create(320,240) + set_flush_cb(flush_cb) + set_draw_bufs 双缓冲（宽高读 dtree，骨架 null 冒烟版已实现）
└─ flush_cb(disp, area, color_p):
   ├─ st7789_set_window(area)          // 现有窗口地址命令
   ├─ st7789_blit_dma(area, color_p)   // 复用现有 24 行分片 DMA 发送器
   └─ 片间总线让出 W25Q128（现有机制保留，8.3 有并发分析）
```

- **局部刷新模式**（非全帧 direct_mode）：320×240 全帧 150KB @40MHz SPI ≈ 30ms/帧（约 33fps 上限），交互场景 LVGL 脏区通常 <1/4 屏，实际 40-60fps；
- 背光 **LEDC PWM**（GPIO38，5kHz/10bit），`display_set_brightness(%)` 归 power_srv 管（12.3），替换现有 `st7789_set_backlight` 的开关式 GPIO；
- PSRAM 大图（照片帧）路径：解码目标缓冲在 PSRAM，flush_cb 里对 PSRAM 源用 GDMA（S3 支持 PSRAM→SPI DMA，需对齐），不满足对齐时 memcpy 到内部弹跳缓冲——**弹跳缓冲 8KB 预分配**。

### 6.3 cst816s 对接 LVGL（port/indev_port.c）

现状缺口：20ms tasker 轮询 + INT 引脚配置了但从未使用；无 indev 适配。

设计：

```
gpio_install_isr_service 一次性
├─ INT(GPIO18) 下降沿 ISR → vTaskNotifyGiveFromISR(s_touch_task)
├─ s_touch_task（4KB，优先级 6，Core1，事件驱动零轮询）:
│    收到通知 → cst816s_read()（I2C 读 0x01-0x06）→ 写入双态点缓冲
│    └─ 30ms 无新通知 → 发松开事件（去抖/抬手判定）
├─ lv_indev_drv: read_cb 返回最近点状态（pressed/released + xy）
│    └─ X/Y 坐标按 rotation 变换（原生 240×320 面板转 320×240 横屏，两个轴都需映射）
└─ 手势（cst816s_get_gesture 上滑/双击等）→ EVENT_TOUCH_GESTURE
     → power_srv 订阅（抬手/双击唤醒，12.3）
```

I2C 读取 ~400kHz 下 6 字节约 0.2ms，中断驱动后 CPU 占用可忽略（对比轮询 50 次/秒×I2C 事务）。

### 6.4 六层架构落地与页面设计

六层 README 的设计约定保留执行，从零实现。**页面清单与导航：**

| # | 页面 page_* | 内容 | presenter / bridge 依赖 |
|---|-------------|------|--------------------------|
| 1 | home | 时钟大字、日期、温湿度挂件、网络状态栏、相册入口 | bridge_time / bridge_sensor / bridge_wifi |
| 2 | photos | 图片轮换（自动播放+手动滑动）、相册网格 | bridge_media |
| 3 | music | 音频播放列表（/audio）、播放控制、录音入口 | bridge_audio |
| 4 | video | 视频文件列表 + 播放页（全屏 MJPEG） | bridge_media |
| 5 | intercom | 对讲主页：PTT 大按钮、呼入提示、留言列表、LoRa 设备列表 | bridge_intercom |
| 6 | clock | 闹钟/定时器列表 + 新建编辑 | bridge_time |
| 7 | settings | WiFi 连接页（扫描列表+密码键盘）、蓝牙配网入口、亮度、关于 | bridge_wifi / bridge_prov / bridge_power |

导航（模仿手机滑动）：
- `lv_tileview` 三横页主框架：`[intercom] [home] [clock]`，边缘 snap + 滑动动画（LVGL 自带滚动动画足够）；
- settings 从 home 状态栏图标推入**页面栈**（`nav_push`/`nav_pop`，带过场动画）；photos/music/video 从 home 挂件推入；
- navigator 层提供：`nav_init() / nav_register(page_desc) / nav_push(id) / nav_pop() / nav_goto(tile)`；页面生命周期回调 `on_create/on_show/on_hide/on_destroy`——**on_hide 时页面停掉自己的 lv_timer**（相册轮播、视频播放页离开即暂停），这是帧率与功耗的关键纪律；
- presenters 层持有页面状态，订阅事件后调用 `lv_obj` 更新接口（经 ui 层暴露），**不在事件回调里直接碰 LVGL**：事件回调 post 到 lvgl_task 队列（`lv_async_call` 或自建消息队列），保证所有 LVGL 调用都在 lvgl_task 单线程内（LVGL 非线程安全）。

### 6.5 各 UI 功能实现方案

| 功能 | 方案 | 关键点 |
|------|------|--------|
| 图片轮换 | /media JPEG 列表 → `esp_jpeg` 解码（硬解）→ PSRAM RGB565 帧 → `lv_image` 换源 + 淡入淡出 | 切换间隔可配；解码 ~20ms/张（320×240）；解码线程 media_player，不在 lvgl_task |
| 视频播放 | 自定义 `.mjp` 容器（帧索引表 + JPEG 帧 [+ 可选 16k PCM 音轨]），`media_player` 20fps 解码 + audio_srv 同步播放 | S3 硬解 JPEG ~30fps@320×240 有余量；文件由 Web 上传，工具脚本 `scripts/make_mjp.py`（ffmpeg 抽帧+压 JPEG+打包） |
| 待机动画 | 低帧（8-10fps）小尺寸（如 160×186）RGB565 帧序列 bin（预制转换，`scripts/png_to_frames.py`），PSRAM 加载循环播放 | 待机时 LVGL 只跑这一个动画 timer；背光降至 10-20% |
| WiFi UI | bridge_wifi 调 `/api/wifi/*` 同源逻辑：设备端直接调 `net_mgr`/wifi 驱动 API（同进程无需 HTTP） | 扫描结果 lv_list；密码 lv_keyboard；连接中态 + 30s 回退倒计时（复用 net_provision 逻辑） |
| 蓝牙配网 UI | settings 入口页：展示配网二维码/设备名 + 等待状态（bridge_prov → ble_prov 模块） | 9.2 |
| 闹钟/定时器 UI | clock 页列表 + lv_roller/lv_textarea 编辑 | 触发时 power_srv 强制亮屏 + audio_srv 响铃（十三场景 7） |
| 对讲 UI | PTT 按下=录音+编码+发送；呼入=全屏提示+自动亮屏；留言列表可回放 | 8.4 |
| 音视频播放 UI | music/video 页：进度条、暂停/继续、音量 | media_player/audio_srv 事件驱动进度刷新（500ms 节流） |
| 中文字体 | `lv_font_conv`（或官方工具）生成常用 3500 字 + ASCII 的 16/24px 字体 bin 放 /font，`lv_font_load("S:/font/cn24.bin")` | 避免编进固件（省 ~500KB flash）；开机首启检测缺失则回退内置 Montserrat |

### 6.6 UI 性能预算

| 指标 | 预算 | 依据 |
|------|------|------|
| 滑动帧率 | ≥30fps（tileview 拖动） | 全帧 27ms 上限 + 局部脏区 |
| 页面切换 | ≤250ms 可感知 | 动画 200ms + 构建 <50ms |
| home 页刷新 | 1Hz（时钟）/ 事件驱动 | lv_timer 1000ms |
| 图片切换 | ≤300ms（含解码+淡入） | 硬解 15ms + IO |
| 视频播放 | 20fps 稳定（音画同步 ±80ms） | 硬解 33ms/帧余量充足 |
| lvgl_task CPU | 交互 <25%，静态 <3% | 脏区机制 + on_hide 停 timer |

---

## 七、音频子系统设计

### 7.1 定位与硬件通路

新模块 `modules/audio_srv`：**统一音频管线服务**，替换 audio_module 骨架（i2s_drv 保留复用）。硬件：INMP441（I2S RX）+ MAX98357A（I2S TX），16kHz/16bit/mono，全双工（BCLK/WS 共享）。

### 7.2 管线架构

```
录音侧（拉）:
  I2S DMA → [audio_in_task: 32ms 帧=64B PCM] → recorder 环形队列(PSRAM, 4s)
                                        │
              ┌─────────────────────────┼──────────────────────────┐
              ▼                         ▼                          ▼
        文件落盘(.wav/.adpcm)      Codec2 编码→lora_proto      WS/HTTP 流(Web 远程监听,后期)
 
播放侧（推）:
  播放源（文件/Web/响铃/tone/对讲解码流）
      → [mixer: 最多2路软件混音+音量] → audio_out_task → I2S DMA → MAX98357A
```

要点：
- **音频数据不过 event_bus**（256B 上限）：帧数据走 FreeRTOS 队列/环形缓冲，事件总线只发 `AUDIO_RECORD_STARTED/STOPPED`、`AUDIO_PLAY_DONE` 等元事件（修正 audio_module 事件带裸指针的生命周期缺陷）；
- `audio_out_task` 优先级 5 高于 lvgl_task，保证播放不卡顿；
- 响铃/提示音：内置小 wav（内部 flash 嵌入 C 数组）与文件播放共用 mixer；
- 录音帧 32ms（512 samples）：对讲端到端延迟预算的核心项（8.5）。

### 7.3 编码决策

| 用途 | 编码 | 码率 | 理由 |
|------|------|------|------|
| 本地录音/留言存储 | IMA-ADPCM | 32kbps（8kHz）或 16kbps（4bit/4kHz 降采样） | 无专利、整数运算极轻、4:1；6MB /audio ≈ 25-50 分钟 |
| LoRa 对讲 | **Codec2 1200bps 模式** | 1.2kbps | LoRa SF7 空口有效载荷 ~4kbps，唯一现实选择（8.5 论证）；vendored `thirdparty/codec2`（含 ARM/neon 优化关闭，纯 C 模式 ~15 MIPS，S3 可承受单编解码） |
| Web 远程播放语音 | MP3 | — | helix mp3 解码器（`esp_audio` 生态）或直接 WAV 免解码，Phase 5 决策 |
| 提示音 | WAV/PCM | — | 免解码直接 I2S |

### 7.4 对外接口（audio_srv.h）

```c
audio_err_t audio_srv_init(void);                    // holder 注册
// 录音
audio_rec_t* audio_rec_start(const audio_rec_cfg_t* cfg);   // 目标: 文件/环形队列/编码流回调
int          audio_rec_stop(audio_rec_t*);
// 播放（返回播放句柄，用于暂停/停止）
audio_play_t* audio_play_file(const char* path);     // wav/adpcm/mp3 按扩展名分派
audio_play_t* audio_play_tone(tone_id_t id);         // 内置提示音
int           audio_play_stop(audio_play_t*);
int           audio_vol_set(int percent);            // 软件音量 0-100（MAX98357A 无硬件音量）
// 对讲流接口（lora_proto 使用）
audio_err_t audio_intercom_sink(codec2_frame_cb_t cb);   // 录音→编码帧回调
audio_err_t audio_intercom_feed(const uint8_t* frame);   // 编码帧→播放队列（低延迟直通，不过mixer音量）
```

---

## 八、LoRa 对讲子系统设计

### 8.1 现状与差距

`modules/lora`（524 行）只实现了 UART 透传收发 + AUX 等待 + tasker 轮询接收；`lora_config_t`（地址/信道/空速）定义了但**没有进入配置模式的代码**（M0/M1 组合时序缺失）——模块参数全靠出厂默认；无协议层。

### 8.2 lora 驱动补全（modules/lora 改造）

1. **模块配置**：init 时 M0=M1=1 进配置模式，写寄存器（地址高/低、信道、空速、发射功率、定点模式），读回校验，退出回透传模式（M0=M1=0）。参数来自 dtree `lora` 节点（补 `address`、`channel`、`air_rate` 字段）；
2. **接收改事件驱动**：UART event queue（uart_drv 已具备）+ AUX 边沿中断替代 tasker 100ms 轮询——对讲延迟直降 100ms 抖动；
3. 发送保持现有 AUX 握手逻辑（等待空闲→发→等发送完成）；
4. 公开回调注册（`lora_register_rx_callback` 已有）+ `EVENT_LORA_RX` 事件并存，lora_proto 用后者。

### 8.3 协议栈（新模块 modules/lora_proto）

对齐并修订 `lora_protocol.md`（v2 修订点见 8.5）：

```
应用层    对讲会话(PTT) │ 留言(存/取/转发) │ 信标发现 │ 遥测(电量/状态)
传输层    分帧 + 序号 + CRC16 + (留言类)停等重传 + (语音类)前向纠错靠冗余
链路层    LR22 透传 UART 流（定点模式帧 ≤ 200B 载荷）
```

- **帧头**：`SYNC(2B 0xAA55) | ver(1) | type(1) | dst(2) | src(2) | seq(2) | len(1) | payload(≤180B) | crc16(2)`——透传模式对方收到的是裸 UART 流，必须自带同步与 CRC（初稿协议无同步头，易失步）；
- **语音帧**：payload = Codec2 1200bps 40ms 帧（6B）× 25 打包 = 150B/帧包 ≈ 160ms 音频/包，配合 seq 检测丢包（丢包→静音填充，不重传，实时语音禁重传）；
- **信标**：30s 周期广播（低占空比），内容含设备名/电量/状态；收到对方信标→设备列表（UI + Web 显示）；
- **留言**：停等协议分片传输（48B/片），接收端按 message_id 重组后落盘 `/audio/msg_<id>_<from>.adpcm`，UI 未读角标 + `EVENT_INTERCOM_MSG_IN`；
- **对讲会话状态机**：`IDLE → CALL_REQ(发 CONTROL) → TALKING(PTT 半双工：本端讲/对端讲互斥) → IDLE`；呼入 10s 未应答自动转留言提示（对齐产品"无人接听存留言"）。

### 8.4 与音频/UI/Web 联动

```
PTT 按下(UI) ──► intercom presenter ──► lora_proto: 会话+PTT_START
              ──► audio_srv: audio_intercom_sink() 挂编码回调
Codec2帧 ──► lora_send() ──► UART/空口
空口 → lora 驱动(UART事件) → EVENT_LORA_RX → lora_proto 解帧
      → 语音帧: audio_intercom_feed() → I2S 立即播放（累积 ≤80ms 抖动缓冲）
      → 控制帧: 会话状态机推进 → EVENT_INTERCOM_STATE → UI（呼入页/通话中态）+ WS 推送
```

### 8.5 带宽现实约束（协议 v2 的核心修订依据）

SF7/BW125/CR4/5 空口速率 ≈ 5.47kbps，透传帧头/前导码开销后有效 ≈ 4kbps：
- Codec2 1200bps + 帧头/间隔冗余 → 占用 ~1.6kbps，**可行**（Meshtastic 同方案验证）；
- Codec2 2400bps → 占用 ~3.2kbps，勉强、丢包敏感，作为"近距离高音质"可选档；
- ADPCM 16-32kbps → **完全不可行**（初稿协议的 PCM/ADPCM 假设需废弃，仅保留给本地存储）；
- dtree 现配置 **SF12**（~0.25kbps）只能跑信标/文本留言；**语音会话时 lora_proto 通过配置模式动态切 SF7**（8.2 的配置能力因此是硬需求）；
- 3km 城市目标在 SF7 下不可达，产品宣传口径应为"城区 0.5-1km / 郊区 1-3km（SF12 文本）/ 语音 SF7 近距离"——文档同步修订。

---

## 九、网络与蓝牙配网设计

### 9.1 net_mgr 保留 + 增强

现结构（STA/AP 状态机、NVS 三级凭据、热切换回退、provider 接口）全部保留，增强四点：

1. **配网体验**：启动 STA 连接失败（WIFI_DISCONNECTED 且无 NVS 凭据）超时 30s → 自动 `net_mgr_start(NET_MODE_AP)` 起配网热点（现需手动切），配网成功自动回 STA——配合"无凭据首启即配网态"的产品流；
2. `NET_STATE_FAILED` 后状态回 `CONNECTING`（重连中语义修正）；
3. 补 `net_mgr_deinit`（event handler 反注册）；
4. mDNS 端口 80 与 `WEB_PORT` 宏解耦（引用 web.h 或常量集中）。

### 9.2 BLE 配网（新模块 modules/ble_prov）

**决策：采用 IDF 官方 `wifi_prov_mgr`（BLE 传输 + protocomm），不自研 GATT 协议。**

- 手机端用现成 **ESP BLE Provisioning** App（Android/iOS 均有），扫二维码即配——自研协议则还要写 App；
- 对接点：`wifi_prov_mgr` 的 `app_prov_get_wifi_config` 回调里调 `net_provision_submit(ssid, pass)`，**无缝挂入现有 provider 体系**（web provider 已是先例）；
- 生命周期：仅配网态运行——`EVENT_WIFI_CONNECTED` → `wifi_prov_mgr_stop()` + `esp_bt_controller_deinit()`（**蓝牙不用即全关**，省 ~45KB 内存与功耗，见十二）；
- 安全：prov 版本 v1、PoP（Proof of Possession）PIN 码印在设备标签/Web 关于页；
- UI：settings 页"蓝牙配网"开关 + 等待动画（bridge_prov 轮询 ble_prov 状态事件）。

### 9.3 WiFi/BLE 共存与内存

- `CONFIG_SW_COEXIST_ENABLE=y`（配网期 BLE 与 AP 热点并存所需）；
- **NimBLE**（禁 Bluedroid）：控制器+主机 ~45KB vs Bluedroid 120KB+；
- 蓝牙任务绑 Core0（与 WiFi 同核，coex 调度器假设）；
- 预期影响：配网期 WiFi 吞吐降 ~30%（共存分时），配网完成蓝牙下线后恢复。

### 9.4 安全基线

- ovs.dtb.json 出厂 WiFi 密码改占位符，首启走配网（开发板可本地改 dtb）；
- Web 端点后期加 token（首启生成，印在设备/Web 关于页，AP 模式直连配置）——Phase 6 项。

---

## 十、Web 上位机扩展设计

### 10.1 保留基础

Mongoose 单任务事件驱动、EXACT+STREAM 双路由表、WS 队列桥接、SPIFFS 网页热更新——全部保留。改造三点：JSON 解析统一 cJSON（替换手写字节匹配）、静态文件加 `Cache-Control` 与 gzip（fs_to_c.py 输出 .gz，Mongoose 按头解压/直发——SPIFFS 空间减半）、`web_server_stop` 主动关 listener。

### 10.2 新增 API（全部走现有路由注册表）

| 端点 | 方法 | 说明 | 后端依赖 |
|------|------|------|----------|
| `/api/fs/list?path=` | GET | 列目录（/audio /media /font） | ovs_vfs |
| `/api/fs/upload?path=` | POST | **STREAM 路由**流式写文件（复用 OTA 上传通道机制，任意大小不占内存） | ovs_vfs |
| `/api/fs/download?path=` | GET | 分块下载（Content-Length 已知） | ovs_vfs |
| `/api/fs/delete?path=` | POST | 删除文件/空目录 | ovs_vfs |
| `/api/fs/stat?path=` | GET | 空间占用（vfs_get_mount_info） | ovs_vfs |
| `/api/alarm/list` / `add` / `del` | GET/POST | 闹钟 CRUD（JSON） | time_srv |
| `/api/audio/play?path=` / `stop` / `vol` | POST | 远程播放设备上的音频文件 | audio_srv |
| `/api/media/play?path=` / `stop` | POST | 远程播放视频（设备屏幕上） | media_player |
| `/api/tts/announce` | POST | 文本→提示播报（后期 TTS，占位） | — |
| `/api/intercom/status` / `ptt` | GET/POST | 对讲状态/远程触发 PTT（同 Web 麦克风推流为后期项） | lora_proto |
| `/api/power/state` / `brightness` | GET/POST | 电源态/亮度查询控制 | power_srv |
| `/api/modules` | GET | holder 全模块状态（init 耗时/状态/错误） | holder |
| `/ws`（增强） | — | 推送增量：现每秒全量 → 事件驱动 + 1s 心跳合并 | event_bus 订阅 |

### 10.3 前端扩展（web/vue-ui）

新增三个页面（延续 Options API + 本地 page 切换风格，避免引入路由库增加体积）：

- **FileManager.vue**：面包屑 + 文件表格 + 拖拽上传（复用 OtaUpdate.vue 的 XHR 进度模式）+ 下载/删除；上传目标分区下拉（/audio /media /font）；
- **Alarm.vue**：闹钟列表 + 时间选择器 + 重复规则（一次/每天/工作日）→ `/api/alarm/*`；
- **Media.vue**：远端文件列表 + "在设备上播放"按钮 + 音量滑条 + 对讲状态卡。

构建链不变（mybuild.sh → fs_to_c.py → web_data.c），新增 gzip 后 web_data 预计 706KB → ~300KB，同时缓解 B5。

---

## 十一、时间服务设计（时钟/闹钟/定时器）

新模块 `modules/time_srv`——无 RTC 芯片（硬件无 DS3231），走 SNTP + 掉电靠每次联网校时：

```
时间源状态机: UNSYNC → SYNCING → SYNCED（EVENT_WIFI_GOT_IP 触发 esp_netif_sntp 启动）
             └─ 掉线保持走时（esp_timer 累计），重连再校
时区/夏令时: setenv TZ + tzset，时区存 NVS（Web/UI 可改）

闹钟: /config/alarms.json（cJSON 数组: {id, hh, mm, repeat, enabled, label}）
      time_srv 每秒比对（tasker middle 任务，SYNCED 才比对）
      触发 → EVENT_TIME_ALARM(id) → power_srv 强制亮屏 + audio_srv 响铃
      → UI 闹钟弹窗（贪睡 5 分钟/停止）
定时器: 纯 RAM（不持久化），UI/Web 设置 → esp_timer 一次性
      → EVENT_TIME_TIMER_DONE → 响铃 + 弹窗
秒级走时广播: 不广播（UI 用本地 lv_timer 读 time_srv_get_now()，避免事件洪峰）
```

API：`time_srv_init / get_now(struct tm*) / is_synced() / alarm_add/del/list/enable / timer_start(min) `。

---

## 十二、电源管理设计

### 12.1 电源状态机（power_srv）

```
            任一交互/呼入/闹钟/Web请求
 ACTIVE ◄─────────────────────────────┐ (亮度100%)
   │无触摸30s                          │
   ▼                                  │
 IDLE_DIM (亮度30%, 呼吸灯)            │
   │无触摸2min(可配)                   │
   ▼                                  │
 STANDBY (背光灭, 待机动画10%亮度或全灭,│
          WiFi modem sleep, CPU降频)   │
   │无事件30min 且无未完任务            │
   ▼                                  │
 LIGHT_SLEEP (深待机, 触摸INT/AUX/RTC唤醒)
```

- 迁移全部由**事件驱动**：订阅 `EVENT_TOUCH_* / EVENT_INTERCOM_* / EVENT_TIME_ALARM / Web 请求钩子`；
- `CONFIG_PM_ENABLE` + `esp_pm_configure(max=240, min=80)` + WiFi modem sleep（`esp_wifi_set_ps(WIFI_PS_MIN_MODEM)`）；lvgl_task 与 audio_srv 持 `PM_APB_FREQ_MAX` lock 仅在活动期；
- **LIGHT_SLEEP 进入条件严格**：无播放、无对讲、无 OTA、无 Web 客户端连接（WS 连接数>0 则停留在 STANDBY）；唤醒源 = 触摸 INT(GPIO18)、LoRa AUX(GPIO46)、RTC 定时（下一闹钟前 5s 预醒）；
- 蓝牙仅在配网态存在（9.2）。

### 12.2 与 LVGL 的配合

- STANDBY 进出：lvgl_task 的 `lv_timer_handler` 周期 5ms→100ms（省 CPU），页面栈压入待机动画页，恢复时弹出；LVGL 自带 `lv_display_set_render_timer` 思路或手动调 `lv_timer_set_period`；
- 背光渐变：LEDC 占空比 20ms 步进软渐变（灭→亮 300ms）。

### 12.3 背光接口

`power_srv_brightness_set(0-100)`（LEDC），st7789 驱动不再直接操作背光 GPIO（职责移交，6.2）。

### 12.4 状态指示灯

并入 power_srv：dtree `leds.status`（GPIO4）呼吸/慢闪/快闪/灭 映射电源态与配网态（替代被删的 led 驱动，C11）。

### 12.5 电流预算（估算，Phase 6 实测校准）

| 状态 | 估算电流 | 构成 |
|------|----------|------|
| ACTIVE（WiFi 在线+亮屏） | ~140-190mA | CPU 40m + WiFi 60-90m + 背光 20-50m + 外设 ~15m |
| IDLE_DIM | ~110-140mA | 同上背光降 |
| STANDBY（modem sleep+灭屏） | ~35-60mA | CPU 80MHz 降频 + WiFi 间歇 |
| LIGHT_SLEEP | ~2-5mA | 8MB PSRAM 保持 ~1-2mA + RTC/唤醒域 |

（供电为 5V 电源模块，无电池压力；预算用于发热与长期可靠性评估。）

---

## 十三、模块间协作：关键场景时序

### 场景 1：开机（holder 编排）

```
app_main: NVS → SPIFFS(设备树回退源) → holder_init → holder_init_all
  批0 logger/event_bus/tasker/dtree ──► 批1 w25q128→ovs_vfs(/font /audio /media /config)
  ──► 批2 net_mgr(STA) → ota(15s确认定时器) → web(路由表+WS)
  ──► 批3 st7789→cst816s(中断)→ath30→audio_srv
  ──► 批4 time_srv→power_srv→lora_proto(模块配置SF切换)
  ──► 批5 lvgl_app(字体加载→nav_init→home页)→intercom
UI 开机页显示 holder 各模块状态 → 2s 后转 home
```

### 场景 2：蓝牙配网（首启无凭据）

```
net_mgr: STA 无凭据 30s → EVENT_WIFI_MODE_CHANGED(ap)
ble_prov: AP 态自动启动 wifi_prov_mgr(BLE广播 "OVS-PROV_xxxx")
手机 App 扫码 → BLE 写 SSID/PASS → net_provision_submit()
  → NVS 持久化 + 切 STA(30s回退保护) → GOT_IP → EVENT_WIFI_CONNECTED
  → ble_prov 停蓝牙 / time_srv SNTP / UI 状态栏已连接 / WS 推送
```

### 场景 3：触摸交互与页面滑动

```
指触屏 → CST816S INT → touch_task(I2C读点) → lv_indev read_cb
 → LVGL 滚动处理 → tileview 页间拖动 → 松手 snap 动画
 → nav 层 on_hide(前页停timer) / on_show(新页起timer)
 同一 INT → power_srv: 任一电源态回 ACTIVE(亮屏)
```

### 场景 4：LoRa 对讲（本端发起）

```
UI PTT按下 → intercom presenter → lora_proto: CONTROL(call_req)
  → 对端 CONTROL(ack) → TALKING
  → audio_srv intercom_sink: PCM帧(32ms) → Codec2(40ms帧6B) → 攒25帧
  → lora_proto 打包(150B载荷) → lora_send(AUX握手) → 空口(SF7)
对端: UART事件 → 解帧 → EVENT_INTERCOM_STATE(对方讲话)
  → audio_intercom_feed → 抖动缓冲80ms → I2S 播放
  → UI 通话页(声浪动画) 
PTT松开 → CONTROL(eot) → IDLE；对端 10s 无 eot → 超时转留言提示
```

### 场景 5：Web 上传图片 → 设备相册

```
浏览器 FileManager 拖入 photo.jpg → POST /api/fs/upload?path=/media
 → STREAM 路由接管连接 → 流式写 littlefs（每块64KB, 总量无关内存）
 → 完成 → WS 推送 fs_changed → （若正在 photos 页）presenter 刷新列表
```

### 场景 6：闹钟触发

```
time_srv 每秒比对 → 到点 EVENT_TIME_ALARM
 → power_srv: 亮屏(渐亮300ms) → audio_srv: play_tone(ALARM) 循环
 → UI 闹钟全屏弹窗(贪睡/停止) → 停止 → tone stop → 2min 后回 IDLE_DIM
```

### 场景 7：待机与唤醒

```
2min 无触摸 → EVENT_POWER_STATE(standby)
 → lvgl 压入待机动画页(10fps) + timer 5ms→100ms + 背光10%
 → WiFi modem sleep；30min 无事件 → LIGHT_SLEEP
触摸INT/AUX/RTC → 唤醒 → ACTIVE → 背光渐亮 + 动画页弹出 → home
```

### 场景 8：OTA（既有，不变）

web_ota OVSO 容器 → app 流式写槽 + dtb 写非活动槽 → 15s 确认翻转 → 全链路保持现状（本次重构不触碰 OTA 线）。

---

## 十四、性能优化专项

### 14.1 内存布局预算（16MB Flash + 8MB PSRAM + 512KB SRAM）

**内部 SRAM（~500KB 可用堆 + 静态）**——DMA 与实时敏感区：

| 用途 | 预算 |
|------|------|
| WiFi+lwIP+系统 | ~120KB |
| event_bus 池+队列 | ~14KB |
| tasker 节点池+worker 栈 | ~26KB |
| LVGL draw_buf ×2 | 38KB |
| w25q128 DMA 缓冲(修复后) | ~8KB |
| jpeg 弹跳缓冲 | 8KB |
| 各任务栈合计 | ~80KB |
| 余量 | >150KB（BLE 上线再 -45KB，仍足） |

**PSRAM（8MB）**——大缓冲与缓存：

| 用途 | 预算 |
|------|------|
| LVGL 堆（`LV_MEM_CUSTOM`→malloc+PSRAM 优先） | 2MB |
| LVGL 图片缓存（lv_cache） | 3MB |
| JPEG 解码帧缓冲（双帧 320×240×3×2） | ~900KB |
| 音频环形队列（录 4s） | 128KB |
| 对讲抖动缓冲 | 16KB |
| cJSON 设备树 + 余量 | >1.5MB |

`sdkconfig`：`MALLOC_ALWAYSINTERNAL=16384`（小对象留内部）保持；新增 `CONFIG_SPIRAM_TRY_ALLOCATE` 策略仅用于显式大缓冲 API（`heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`），避免全局 PSRAM 化带来的内部堆枯竭。

### 14.2 SPI 总线调度（LCD + W25Q128 共线，永久约束）

- 保持现有**分片 DMA + 片间让出**机制（st7789_flush 24 行/片），它是共线方案的基石；
- 新增纪律：视频播放（持续刷屏）期间，/media 读取与刷屏在同一 media_player 任务内**交替调度**（读一帧→刷一帧），避免跨任务争抢；VFS 文件读内部已有 w25q128 设备锁（s_dev_mutex）保证串行；
- 大文件 Web 上传（写 flash）与刷屏并发时，SPI 分片让出保证 UI 不卡帧（尾延迟 ≤3ms 机制已验证）；
- **不拆 SPI2/SPI3**（PCB 定型），此为既定事实，文档明确不再作为选项。

### 14.3 任务优先级总表

（见 4.2）关键约束：audio_out(5) > touch(6 实际最高) > event_bus(5) > lvgl(4) > web/tasker(3) > jpeg(3)；对讲期 lora_rx(4) 与 audio 同级防饿死。

### 14.4 网络/OTA 吞吐

- LWIP：`TCP_SND_BUF` 5760→11520、`TCP_WND_DEFAULT` 同步调、`TCP_MSS` 1436 保持、开 `TCP_SACK`；
- Mongoose 静态资源 gzip（10.3）+ 浏览器缓存，OTA 推送速度预期 700KB/s → 1MB/s+；
- 这些是 Phase 5 可选项，实测瓶颈不在 WiFi 时不动。

### 14.5 分区表重排（Phase 0b 执行，需串口烧录一次）

> v1.1 实测更新：固件含 LVGL 后 1.54MB / ota 分区余 41%（且 sdkconfig 仍为 Debug 优化，切 -Os 还会缩减）。紧迫度从"必爆"降为"Phase 4（JPEG+BLE）之前必须"；仍建议尽早（0b）执行——分区表无法 OTA，改布局只能串口烧。

```
# 现状问题: ota 2.5MB 偏小(内嵌web资产706KB+LVGL+蓝牙)；littlefs(/config) 8.75MB 严重超配
# 新布局: littlefs 缩到 1MB(/config 配置+日志)，释放 7.75MB 全部给 app
nvs       0x9000   0x5000      (不变)
otadata   0xe000   0x2000      (不变)
phy_init  0x10000  0x1000      (不变)
ota_0     0x20000  0x3D0000    (2.5MB → 3.8125MB)
ota_1     0x3F0000 0x3D0000    (2.5MB → 3.8125MB)
spiffs    0x7C0000 0x200000    (不变: web资产+设备树出厂回退)
dtb_0     0x9C0000 0x10000     (不变)
dtb_1     0x9D0000 0x10000     (不变)
littlefs  0x9E0000 0x100000    (8.75MB → 1MB, /config)
# 合计 0x9E0000+0x100000 = 0xAE0000 < 16MB, 尾部 5MB 预留(将来字体库分区/资产OTA区)
```

配套：`OVS_MEDIA_FORMAT_ON_FIRST_BOOT` 置 1 一次格式化 littlefs（现开关现成）；/config 数据迁移（alarms.json 等尚不存在，无迁移负担）；`docs` 分区说明同步。

### 14.6 编译与配置基线

- 发布用 `sdkconfig.defaults.release`：`COMPILER_OPTIMIZATION_SIZE`、`LOG_MASTER_LEVEL=WARNING`（串口静默）、关 assertions 降级；
- Flash QIO、PSRAM 80MHz：Phase 2 冒烟（全链路刷屏+VFS 压测 1 小时）后写入 defaults。

---

## 十五、实施路线图

> 每阶段独立可验证、可 OTA 交付；阶段内任务可并行。验收标准以真机为准，完成即写开发日志。

### Phase 0：清理与地基修复（v1.1 拆分 0a/0b）

**Phase 0a：纯删除类清理（1 个会话内）**
1. include/ 解散（5.6；C4 lvgl.h 已完成）+ eventbus_api/littlefs 拷贝/空壳文件/备份文件删除（C5-C10）；
2. 测试代码迁出固件（C8，迁入 tests/ 作 PC 回归测试，兼作 5.1/5.2 门禁）。
   **验收**：全量构建通过；WiFi/OTA/VFS/Web 回归全通过。

**Phase 0b：分区表重排（独立交付，需串口烧录一次，见 14.5）**
1. 新分区布局烧录 + `OVS_MEDIA_FORMAT_ON_FIRST_BOOT` 过渡格式化；
2. OTA 双槽回归验证。
   **验收**：OTA v1→v2 升级回滚演练通过；littlefs 1MB 挂载正常。

**地基 bug 修复（0a/0b 之间穿插）**：heartbeat 接线（C12）、dtree 四修复（5.5）、ovs_vfs 加锁（5.4）、w25q128 缓冲与文档（C13/C14）；~~logger 切 esp_log（5.3）~~ **v1.1 改判取消**。
**验收**：全量构建零警告新增；`/api/status` rssi 非 0。

### Phase 1：核心层与编排（预计 2-3 个会话）

1. event_bus 四修复 + 内存池（5.1）；
2. tasker 三修复 + 依赖统一（5.2）；
3. holder 启用 + app_init.c 注册表 + main.c 瘦身（4.4）+ `/api/modules` 端点。
   **验收**：开机日志打印各模块 init 耗时；拔掉任一可选模块（模拟失败）系统降级运行；订阅者 ≥6。

### Phase 2：显示/触摸/LVGL 运行时（v1.1 修订：骨架已落地，剩余 2-3 个会话，UI 里程碑 M1）

1. ~~引入 LVGL v9~~（**已完成 2026-09-09**：9.5.0 vendored + 六层骨架 + lvgl_task/tick/显示锁 + PC 模拟器；esp_jpeg 移 Phase 4）；
2. st7789 flush_cb 对接 + dtree 宽高 + 背光 LEDC（6.2）——**v1.1 建议提前为独立小里程碑（项目首次"点亮"）**；
3. cst816s 中断改造 + indev 对接（6.3）；
4. ~~lvgl_app 重写~~（已完成：lvgl_task/显示锁/lv_tick_set_cb）+ 中文字体链路（/font + lv_font_load + lv_fs 后端）；
5. Flash QIO / PSRAM 80MHz 冒烟（14.6）。
   **验收**：LVGL 官方 demo 控件页 40fps+；触摸拖动滑块跟手；中文文本渲染；亮度可调。

### Phase 3：UI 应用框架与基础页面（M2）

0. **i18n 多语言框架（v1.1 新增，页面开工前置）**：`_("键")` 宏（`i18n_tr` 实现）、/config/langs/*.json（cJSON→哈希驻 PSRAM）、缺失回退链（当前语言→默认语言→键名+LOGW）、切语言=nav 重建、配套 scripts/i18n_check.py 键集校验、切语言联动 lv_font_load 字体；
1. navigator/presenter/bridge 骨架实码化（6.4；三横页 tileview/bridge_time/home 页骨架已落地，补 nav_push/nav_pop 页面栈与 on_create/on_show/on_hide/on_destroy 生命周期）；
2. tileview 三主页 + settings 栈导航 + 滑动动画；
3. home 页（时钟/温湿度挂件——ath30 此阶段接线/网络状态栏）；
4. settings：WiFi 扫描连接页（复用 net_mgr）、亮度、关于；
5. time_srv + clock 页（闹钟/定时器完整闭环，含响铃弹窗）。
   **验收**：完整人机交互循环——滑动切页、连 WiFi、设闹钟次日触发亮屏响铃。

### Phase 4：媒体与音频（M3）

1. audio_srv 管线（录音文件/播放/提示音/音量）+ IMA-ADPCM 存储编解码（七）；
2. media_player：图片轮换页（JPEG 解码 + 淡入淡出）；
3. 视频链路：`make_mjp.py` 工具 + MJPEG 播放页（静音版）+ 音轨同步版；
4. music 页（列表/播放控制）+ 待机动画资产管线（6.5）。
   **验收**：照片轮换 300ms 内切换；20fps 视频连播 5 分钟无音画漂移；录音 1 分钟回放正常。

### Phase 5：LoRa 对讲与 Web 扩展（M4，两线可并行）

A 线（对讲）：lora 驱动配置模式 + 事件接收（8.2）→ Codec2 引入 + lora_proto 帧层 → 会话状态机 + intercom UI 页 → 留言收发存取 → 信标设备发现。
B 线（Web）：fs API + FileManager.vue + alarm API/页 + 远程播放 API/Media.vue + WS 事件化。
   **验收**：两台真机 PTT 对讲端到端延迟 ≤600ms（含编码 40ms+分包 160ms+空口）；留言断点续传；浏览器上传 8MB 视频到 /media 并在设备播放。

### Phase 6：BLE 配网 + 电源 + 发布（M5）

1. ble_prov（wifi_prov_mgr + NimBLE + coex，9.2）+ settings 配网页 + 首启无凭据自动流程（9.1）；
2. power_srv 完整状态机 + 待机动画 + light sleep + 唤醒链路（十二）；
3. 发布基线：sdkconfig.release、安全基线（9.4）、功耗实测校准（12.5）、文档全量刷新（ARCHITECTURE/PROJECT_STRUCTURE/技能文档 v3.0）。
   **验收**：新设备开箱 5 分钟内完成蓝牙配网到相册播放全流程；待机电流实测入表；OTA 从 v1 到 v2 升级回滚演练。

---

## 十六、风险与关键决策点

| # | 风险/决策 | 影响 | 缓解/结论 |
|---|-----------|------|-----------|
| R1 | LVGL v9 + IDF 6.0.1 兼容性 | ~~Phase 2 阻塞~~ **已关闭（2026-09-09）** | 9.5.0 vendored 路线，真机编译+运行+OTA 冒烟 PASS |
| R2 | QIO/80MHz PSRAM 稳定性 | 花屏/死机 | 冒烟压测门禁，不通过保持 DIO/40M（性能预算已按保守值给） |
| R3 | Codec2 在 240MHz 单核实时性 | 对讲破音 | 1200bps 模式 ~15MIPS 远低于预算；实测不行则编码任务绑 Core1 + 关闭 WMMX 之外的优化 |
| R4 | SF7 下对讲距离 <3km 预期 | 产品口径 | 已在 8.5 修订口径；UI/Web 明示当前空速档位 |
| R5 | MJPEG 容器自研格式生态封闭 | 用户素材转换门槛 | 提供 `make_mjp.py`（ffmpeg 一键转换）；Phase 6 后评估 AVI/MJPEG 标准 demux |
| R6 | 分区重排需串口烧录 | 一次人工操作 | Phase 0 一次性完成，工具 burn.sh 现成 |
| R7 | web 内嵌资产与固件同步膨胀 | app 超限复发 | 分区扩容后 headroom 1.3MB；gzip 静态资源；Phase 6 复查 |
| R8 | BLE+WiFi 共存内存挤压 | 运行期 OOM | NimBLE（非 Bluedroid）；配网完即下线；14.1 预算已含 |
| R9 | holder 启用引入启动回归 | 变砖风险 | Phase 1 单独交付 + required 模块仅 5 个 + OTA 15s 回滚兜底 |
| R10 | 双份事件通路（event_bus vs LVGL 消息队列）职责混乱 | 架构腐化 | 铁律：跨模块=事件总线；进 LVGL=lvgl_task 内 async 调用（6.4），写进 ARCHITECTURE |

---

## 附录 A：过时内容处置总表

（"§2.2/2.3/2.4 编号"交叉引用，此处汇总执行视图）

| 处置 | 项 |
|------|-----|
| **删除文件/目录** | include/（整目录解散：logger.h tasker.h ota.h web.h heartbeat.h event_bus.h dtree.h.bak lvgl.h）；api/eventbus_api/；thirdparty/littlefs-2.11.3/；drivers/gpio/；drivers/led/；五个 *_module.c 空壳；全部 *.bak/backup；scripts/sign_firmware.py；vue HelloWorld.vue；lora_config_t 之外 cst816s_touch_cb_t 死类型 |
| **移动** | include/dtree.h → components/dtbs/；event_bus/tasker 测试 → tests/ 组件（OVS_ENABLE_TESTS） |
| **修复接线** | heartbeat_init（main/app_init）；w25q128 DMA 缓冲；ath30/cst816s/st7789 init（holder 批3） |
| **重写/补齐** | lv_conf.h（v8→v9）；lvgl_app.c；audio_module→audio_srv；lora 配置模式+lora_proto；lvgl stub 删除后真库引入 |
| **配置变更** | 分区表重排；sdkconfig（BT/NimBLE、PM、QIO、80M PSRAM、release 基线）；main/idf_component.yml（lvgl/esp_jpeg） |
| **文档重写** | hardware_notes_for_software.md 引脚表（以 dtree 为源）；lora_protocol.md v2（8.3/8.5）；development_roadmap.md 标注由本文档取代；ARCHITECTURE.md tasker/holder 章随代码同步 |
| **安全处理** | dtb 明文密码→占位符（发布）；Web token（Phase 6） |

## 附录 B：新增事件字典（event_bus_types.h 扩展）

```
MODULE_ID_TIME   0x000B: EVENT_TIME_SYNCED / TIME_ALARM (data: alarm_id) /
                        TIME_TIMER_DONE (data: timer_id) / TIME_ZONE_CHANGED
MODULE_ID_POWER  0x000C: EVENT_POWER_STATE_CHANGED (data: old,new) /
                        POWER_BRIGHTNESS_CHANGED
MODULE_ID_MEDIA  0x000D: EVENT_MEDIA_PLAY_STARTED/DONE/ERROR (data: path句柄) /
                        MEDIA_PROGRESS (UI 不订阅, 播放器页内部查询) / MEDIA_SLIDESHOW_TICK
MODULE_ID_INTERCOM 0x000E: EVENT_INTERCOM_STATE (data: 会话态) /
                        INTERCOM_MSG_IN (data: from,msg_id) / INTERCOM_PEER_FOUND/LOST /
                        INTERCOM_PTT_BEGIN/END
MODULE_ID_FS     0x000F: EVENT_FS_CHANGED (data: mount点) —— Web 上传完成通知 UI
MODULE_ID_PROV   0x0010: EVENT_PROV_STARTED / PROV_CLIENT_CONNECTED / PROV_DONE / PROV_FAILED
沿用现有 MODULE_ID_* 划分；每事件配 data 结构体 + 名表登记（5.1 修复项）
```

## 附录 C：新增组件与文件清单

| 组件/路径 | 新增文件 | 依赖（REQUIRES） |
|-----------|----------|------------------|
| modules/audio_srv | audio_srv.c/.h、rec_pipeline.c、play_mixer.c、codec_adpcm.c | i2s_drv、tasker_api、event_bus、dtbs、logger |
| modules/media_player | media_player.c/.h、mjpeg.c、slideshow.c | esp_jpeg、ovs_vfs、event_bus、logger、tasker_api |
| modules/time_srv | time_srv.c/.h、alarm_store.c | net_mgr(事件)、cJSON、event_bus、logger |
| modules/power_srv | power_srv.c/.h、backlight.c、status_led.c | dtbs、event_bus、logger、esp_timer |
| modules/lora_proto | lora_proto.c/.h、frame.c、session.c、msgbox.c、beacon.c | lora、codec2、event_bus、logger |
| modules/ble_prov | ble_prov.c/.h | net_mgr(net_provision)、event_bus、logger |
| core/mem_pool（v1.1 新增） | mem_pool.c/.h：记账分配(按模块统计峰值)/定长块池/大缓冲助手 | 无（基础件；event_bus 池、音频帧池建于其上） |
| modules/i18n（v1.1 新增） | i18n.c/.h（`_("键")`=i18n_tr）、lang_store.c、lv_fs_vfs.c（ovs_vfs→lv_fs 后端） | cJSON、ovs_vfs、event_bus、logger |
| thirdparty/codec2 | vendored 源（codec2 1200 模式裁剪） | — |
| src/lvgl/port | display_port.c、indev_port.c、lvgl_port.h | lvgl、st7789、cst816s、power_srv |
| src/lvgl 实码化 | navigator/nav.c、pages/page_*.c（7 页）、presenters/*.c、bridge/bridge_*.c、ui/基础控件 | lvgl、各服务模块头 |
| src/app | app_init.c（holder 注册表） | holder、全部模块 |
| scripts | make_mjp.py、png_to_frames.py | — |
| web/vue-ui | FileManager.vue、Alarm.vue、Media.vue | — |
| tests/ | event_bus_test.c、tasker_test.c 迁入 | — |

## 附录 D：v1.1 变更记录（2026-09-09）

依据：LVGL 双平台骨架落地实测 + 用户逐项裁定。实施记录见 `docs/development_log.md` 2026-09-09 条目与 `docs/superpowers/specs/2026-09-09-lvgl-dual-platform-design.md`。

| # | 变更 | 状态 |
|---|------|------|
| 1 | R1（LVGL v9 × IDF 6.0.1）关闭；C4（include/lvgl.h 假 stub）完成 | 已完成 |
| 2 | 6.1 路线变更：9.5.0 vendored + lv_conf.h 双平台配置；**新增 PC 模拟器（sim/）维度** | 已完成 |
| 3 | 全文屏幕参数 240×280 → **320×240 横屏**（实物确认）；触摸改 X/Y 双轴变换 | 已回写 |
| 4 | 5.3 logger 改判：**保留 printf 实现**（PC 端零改造复用），仅删 include/logger.h 旧副本（C1 不变） | 用户裁定 |
| 5 | 5.2 tasker：对外 API **冻结**，内部优化以 tests/ PC 回归测试为门禁 | 用户裁定 |
| 6 | 5.1 event_bus：四修复 + 池建于 mem_pool + **PC 宿主测试层**（ovs_tests）；修复后强制接入真实订阅者 ≥6 | 确认立项 |
| 7 | 十二 power_srv：确认立项（电源状态机/背光 LEDC/状态灯/light sleep） | 确认立项 |
| 8 | 新增 `core/mem_pool`（记账分配/定长块池/大缓冲助手；业务代码禁裸 malloc，LVGL 后期挂自定义分配器） | 新增设计 |
| 9 | 新增 `modules/i18n`（`_("键")` 宏 + /config/langs JSON + 回退链 + 切语言重建导航）；前置 lv_fs 后端 | 新增设计 |
| 10 | 4.2 线程/tasker 分工**终版裁决**（口诀：持续循环/硬实时→任务；间歇秒级→tasker；纯事件→订阅+esp_timer） | 裁定 |
| 11 | **modules/lora、modules/ath30 驱动由用户本人开发**；上层只依赖公共头，接口以驱动需求规格约定，两线互不阻塞 | 边界约定 |
| 12 | Phase 0 拆分 0a（纯删除）/0b（分区重排独立交付）；st7789 点屏提前为独立小里程碑；Phase 2 收缩为 2-3 会话 | 排序 |
| 13 | B5/14.5 flash 紧迫度降级（实测 1.54MB/余 41%），分区重排仍尽早（0b）执行 | 实测修订 |

---

**本方案执行纪律**：每完成一个 Phase 在 `docs/development_log.md` 顶部记条目；架构决策变更（如 R1-R10 的取舍）必须回写本文档对应章节并升版本号；本文档与 `docs/ARCHITECTURE.md` 的分工——本文档管"为什么与怎么改"，ARCHITECTURE 管改完后的"是什么"。
