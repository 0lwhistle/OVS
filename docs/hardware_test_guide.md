# 真机测试指南（三任务共享成果文档）

> **性质**：本轮为"代码+架构"无板开发，验收以编译（idf.py build）与
> PC 门禁（tests/ovs_tests / PC 模拟 / vite build）为准；本文档是交付给
> 项目所有者**后续上板测试用**的唯一指导文档，三任务共同维护。
> **维护规则**：每个任务只编写/更新"自己章节"（[HUB]=§2，[GUI]=§3，
> [WEB]=§4）；§1 全局前置由 [HUB] 维护；§5 已知限制三方 append-only；
> 禁止改删他人章节。
> 每个测试项格式：**前置条件 → 操作步骤 → 预期结果（含串口日志关键字/
> 界面表现）→ 不通过时排查点**。

---

## 1. 全局前置与烧录（[HUB] 维护）

**开发环境初始化**
```bash
source scripts/env.sh        # ESP-IDF v6.0.1 + 项目脚本（mybuild/monitor/ota_push）
```

**构建与烧录（三选一）**
```bash
mybuild.sh                   # 完整构建（Vue 前端+打包+固件）+ 串口烧录
idf.py build && idf.py -p /dev/ttyACM0 flash monitor   # 手动全流程
idf.py build && ota_push.sh 192.168.2.xxx              # 免串口 OTA 迭代
```
监控：`monitor.sh`（或 `idf.py monitor`）。

**PC 门禁（无板即可跑）**
```bash
cmake -S tests -B build-tests && cmake --build build-tests -j && ./build-tests/ovs_tests
bash scripts/sim_build.sh && xvfb-run -a ./build-sim/ovs_ui_smoke   # PC 模拟+UI 冒烟
```

**SPIFFS 资源打包说明（[HUB] 批次①起）**：`idf.py build` 时自动把
`components/dtbs/config/ovs.dtb.json`（设备树）与 `assets/i18n/*.json`
（语言包，构建暂存于 `build/spiffs_root/`）打包进 `build/spiffs.bin`，
烧录命令已含 `0x520000 build/spiffs.bin`；改语言包或设备树后重跑
`idf.py build`（或 ota_push 携带容器）即可，无需手工步骤。

**启动日志模块状态对照表（[APP_INIT] / holder_print_status 输出）**

| 模块名 | required | 含义/缺席影响 |
|---|---|---|
| event_bus/tasker/dtree | ✓ | 核心，失败停机 |
| w25q128/ovs_vfs | ✓ | 存储，失败停机 |
| net_stack | ✓(可选编译) | 网络+Web+OTA（OVS_ENABLE_NET） |
| st7789/cst816s/ath30 | 否 | 无屏/无触摸/无传感器降级 |
| heartbeat | 否 | 心跳缺席 |
| **i18n** | 否 | 读 /spiffs/i18n/zh-CN.json；缺席→`_(label)` 回退键原文 |
| **time_svc** | 否 | 时间服务；缺席→bridge 时间恒 0 灰显 |
| **sensor_cache** | 否 | 订阅传感器事件；缺席→温度/湿度卡"暂无数据" |
| **sysinfo** | 否 | 网络/设备信息查询；缺席→网络卡离线态 |
| lvgl_app | 否 | UI 入口 |

## 2. [HUB] 数据中枢测试项

### 2.1 统一数据接口（i18n / sensor_cache / time_svc / sysinfo）

**T2.1.1 i18n 语言包加载与热切换**
- 前置：烧录含 spiffs.bin（内含 /i18n/zh-CN.json、en-US.json）的固件；
- 步骤：上电观察启动日志 → LVGL 设置页切换语言（中/英）；
- 预期：启动日志 `[I18N]: language pack loaded: /spiffs/i18n/zh-CN.json (N labels)`
  （N=92）；切换语言后页面文案立即变英文/中文；`[WEB][SYSINFO]` 侧
  `GET /i18n/zh-CN.json` 返回 200（浏览器访问 `http://<ip>/i18n/zh-CN.json`
  可见 JSON）；
- 排查：`i18n pack not found`→spiffs.bin 未烧或未重打包；生僻字方框→
  LVGL 内置 CJK 常用字集限制（见 §5）。

**T2.1.2 sensor_cache 快照**
- 前置：ath30 已接（否则本项观察"降级路径"也视为通过）；
- 步骤：上电后进入 LVGL 主页 / Web Dashboard；
- 预期：主页温度/湿度卡与 Web /api/sensor 数值一致（来源同一缓存），
  约 30s（设备树 sample_interval_ms）刷新一次；串口 `[SENSOR_CACHE]`
  无连续 `lock timeout` 告警；
- 排查：显示"暂无数据"→ 看 `[ath30]` 是否采集、`[SENSOR_CACHE]: cached`
  是否出现；两者都有仍无数据→ i2c 总线冲突。

**T2.1.3 time_svc 时间显示**
- 步骤：观察主页大字时钟与 Web /api/time；
- 预期：时钟从 00:00:00 起走（uptime 时钟）；LVGL 时钟灰显、Web
  `"synced":false`（NTP 未接入的预期态）；时间数值照常走不冻结；
- 排查：时钟不走→ `[TIME_SVC]` 未初始化或 bridge 未接真
  （`[BRIDGE]: bridge mock mode` 出现即开关未翻）。

**T2.1.4 sysinfo 网络信息**
- 步骤：主页网络卡 / Web Dashboard 网络卡 / `curl http://<ip>/api/net/info`；
- 预期：三处 mode/ssid/ip/netmask/gw/mac/rssi 一致；AP 模式下 rssi=0、
  ip=192.168.4.1；`[SYSINFO]: sysinfo ready` 出现在启动日志；
- 排查：字段全 0→ net_mgr 未启动或移植层 esp_netif 句柄未取到
  （`esp_netif_get_handle_from_ifkey` 失败会回退 0.0.0.0）。

**T2.1.5（PC 门禁已覆盖）** i18n/加载切换回退节流、sensor_cache 三态、
time_svc、sysinfo mock 查询：`./build-tests/ovs_tests` 308 项全绿
（test_hub.c [HUB-I1~I4] 段）。

### 2.2 ath30 收口

**T2.2.1 周期采集与设备树间隔**
- 前置：ath30 接在 I2C（GPIO16/17），设备树 `temperature_sensor` 节点含
  `sample_interval_ms: 30000`；
- 步骤：上电观察串口 ≥35s；
- 预期：启动日志 `[ath30]: Periodic read started, interval=30000 ms`；
  每 30s 一条 `Temp: xx°C, Humidity: xx%`（DEBUG 级，需 logger
  LOG_DEBUG 时可见）或 Web /api/sensor 的 age_ms 周期性归零重计；
- 排查：`sample_interval_ms not set` LOGW→设备树未含新键（重烧
  spiffs.bin/dtb）；interval=1000→读到旧键 measure_interval_ms。

**T2.2.2 失败停采与恢复**
- 前置：可断开 ath30 的 SDA/SCL 接线（模拟无应答）；
- 步骤：正常运行中断开传感器 ≥3 个采集周期（约 90s）→ 观察日志 →
  重新接上 → 再等 ≤5 个周期（150s）；
- 预期：断开后看到 3 条 `measure failed (x), 1/3、2/3、3/3`，第 3 次
  打 `sampling suspended` 并发布一次 EVENT_SENSOR_ERROR（Web
  Dashboard 显示"暂无数据"）；停采期间无刷屏告警；重接后
  `retrying measure...`→`sensor recovered, sampling resumed`，数据恢复；
- 排查：ERROR 事件刷屏→停采逻辑未生效；不恢复→确认接线与上拉。

**T2.2.3 CRC 校验**（PC 门禁已覆盖真逻辑，板端无需专测）
- mock i2c 注入坏 CRC → `ath30_ERR_CRC`（ovs_tests 325 项中
  [HUB-T1] 段 12 项）。

### 2.3 LoRa lora_tp（需两台设备，可延后）

**前置（两台设备各执行一次）**
- 设备树 `lora.lora_tp` 节点已含（本批已写）：`local_address` 两台分别
  置 1 / 2（**当前两台镜像相同，上板前需手改其中一台的 local_address
  并重烧**）；`ack_timeout_ms_p*`/`tx_gap_ms_p*` 为手册核实前占位值
  （NORMAL=350/300ms），先按 NORMAL 档联调；
- 两台同信道（AT+CHANNEL 一致）、波特率 115200、LBT 开启（AT 层配置
  属驱动任务，本批未动，沿用现状）；
- 启动日志确认：`[LORA_TP]: init ok (addr=0x0001 win=4 q=4 retries=3)`；
  若出现 `lora driver unavailable, lora_tp degraded` → 检查 lora 模组
  UART 接线（GPIO9/10）与 M0/M1/AUX（GPIO8/3/46）。

**T2.3.1 可靠文本传输（UNRELIABLE 单帧）**
- 步骤：两台上电后由应用/调试入口向对端 addr 发 ≤188B 消息；
- 预期：对端 `[LORA_TP]: rx complete` 或应用 rx 回调收到一致内容；
  `curl` 无关，纯板端；发送端日志 `unreliable sent`。

**T2.3.2 可靠传输 8KB（分片+ARQ+CRC32）**
- 步骤：A→B 发送 8KB（RELIABLE，NORMAL 档）；
- 预期：B 收到 8192B 且内容一致（`rx complete ... len=8192`）；A
  `tx done`；全程约 44 片、11 窗；EVENT_LORA_TP_RX_COMPLETE 进事件总线；
- 排查：`ack timeout, window retry` 偶发可接受（重传兜底）；持续超时→
  检查两台信道/地址；`peer lost state ... rewinding` 表示对端曾重启，
  可自愈。

**T2.3.3 断链恢复/大消息 sink**
- 步骤：传输中途遮挡/关断一台天线数秒再恢复；或发 >2048B 消息验证
  sink 路径（应用须事先 set_rx_sink，否则 `reject: large msg`）；
- 预期：短断链（<max_retries×ack_timeout）自动续传完成；超长断链对端
  `tx failed err=-6`（TIMEOUT，属协议预期）；sink 路径 `len=N (sink)`。

**T2.3.4 已核实清单（拿 DX-LR22 手册后修正）**
- LEVEL 档位与 ack_timeout/tx_gap 数值映射（现占位：350/110/5700、
  300/60/5600）；
- 单帧 200B 载荷上限与 AT+PACKET=3 实际值；
- 空口速率对 FAST 档的实际吞吐。

## 3. [GUI] 板端体验测试项

### 3.1 音频（audio_module 异步 + audio_player）

**前置条件**：按 §1 完成烧录（固件含 audio_module/audio_player 组件）；
确认 ovs.dtb.json 含 audio 策略节点（volume_default/buf_frames/queue_depth）
与 i2s0.amplifier 节点（sd_pin）；/audio 分区放入测试 PCM 文件（裸 PCM 头
"OPCM" + rate/ch/bits/data_len + 数据，8k/16k 采样 16bit 单声道）。

**3.1.1 上电自检（模块就绪）**
- 步骤：烧录后上电，monitor.sh 观察启动日志。
- 预期串口日志：`[AUDIO_MODULE]: Audio module initialized successfully
  (ring=4096B, vol=80, sd_pin=-1)` → `[AUDIO_PLAYER]: audio_player ready
  (queue_depth=4, volume=80)`；无 I2S 初始化报错；holder 状态表中音频
  模块无失败。
- 排查点：`I2S device nodes not found` = 设备树缺 i2s-microphone/
  i2s-amplifier 节点或 dtree 加载失败；`audio_module init failed: -x
  (player degraded)` = I2S 硬件初始化失败（查 BCLK/WS/DOUT 接线与
  设备树引脚）。

**3.1.2 外放提示音/文件播放**
- 步骤：触发一次播放（集成提示音钩子或临时在 app 侧调
  audio_player_play_file("/audio/xxx.pcm")）。
- 预期表现：扬声器外放可闻；串口出现 `play start: /audio/xxx.pcm
  rate=16000 ch=1 bits=16 len=xx ms (decoder=pcm-passthrough)`；播放结束
  发布 EVENT_AUDIO_PLAY_DONE。
- 排查点：无声先查功放 SD 脚电平（mute 状态）与 GAIN 跳线；
  `no decoder matched` = 文件缺 "OPCM" 头或参数越界；`open failed: xxx`
  = 文件不存在或 /audio 分区未挂载。

**3.1.3 音量分档与静音（真机试听）**
- 步骤：设置页拖动音量 slider（0/25/50/75/100 五档）听辨；静音经
  audio_player_mute 验证。
- 预期表现：五档可辨且 0=无声；重启后音量保持（NVS 命名空间
  "audio_player"，日志 `volume loaded from nvs: xx`）；mute 时 SD 脚
  拉低（万用表量测，已接线时）。
- 排查点：音量无变化 → 确认 OVS_BRIDGE_AUDIO=1（实调 audio_player）；
  NVS 不保持 → 查 nvs_flash 初始化顺序。

**3.1.4 队列依次播放**
- 步骤：连续触发 ≥3 次播放请求（队列深度默认 4）。
- 预期串口日志：`queued ... (token=N, queue=M)` 递增；逐项
  `play start`；队列满时 `queue full (4)` 返回错误且不影响在播项。
- 排查点：顺序错乱属异常（pump 单 worker 串行）；卡死查 tasker
  （tasker_is_full）与 audio_feed 任务存活。

### 3.2 LVGL 界面（导航/主题/语言/主页数据卡/待机）

**前置条件**：§1 烧录完成；屏幕（ST7789）与触摸（CST816S）正常；
PC 侧已用 ovs_sim（mock 数据）预验界面（本节为板端复核）。

**3.2.1 三页导航**
- 步骤：上电默认进主页 → 点设置齿轮进设置页 → 点返回钮回主页。
- 预期表现：主页（顶栏/大字时钟/温湿度卡/网络卡）↔ 设置页（音量/
  语言/设备名/待机四行）正常切换无残影卡死；启动日志
  `[NAV]: page registered: home/settings`。
- 排查点：白屏查 st7789/背光；触摸无效查 cst816s（app_init 状态表）；
  切页无响应查 `[NAV]` 日志。

**3.2.2 蓝白主题**
- 步骤：目视两页配色。
- 预期表现：页面底 #F5F7FA、白卡片圆角描边、主色 #1E6FFF（按钮/
  滑条/图标）、文本三级深灰（主 #1F2937 / 次 #5B6472 / 占位 #9CA3AF）。
- 排查点：颜色偏紫/偏绿 = RGB565 字节序问题（flush 侧 lv_draw_sw_rgb565_swap）。

**3.2.3 语言热切换**
- 步骤：设置页语言下拉选 en-US → 再切回 zh-CN。
- 预期表现：当前页即时重建为对应语言（HUB 交付后读 /i18n/<lang>.json；
  交付前为 bridge 内置 mock 词表）；串口
  `language switched -> en-US, reload page`。
- 排查点：文字缺字 = CJK 常用字集未覆盖（fonts/ 全字库后续接入）；
  未命中回退原文并 LOGW `label not found: XXX`。

**3.2.4 主页数据卡（HUB 集成后复核）**
- 步骤：等 ath30 采样周期（默认 30s）观察温湿度卡；切换网络模式观察
  网络卡与状态点。
- 预期表现：温度卡 xx.x°C 主值+湿度副值（无数据灰显"暂无数据"）；
  网络卡 STA/AP/OFF + ssid + ip + rssi；状态点绿=在线/红=离线；时钟
  HH:MM 每秒刷新（NTP 未同步时灰显属预期——synced=false 照常显示）。
- 排查点：数值不更新 → 确认 OVS_BRIDGE_HUB_REAL=1 且 sensor_cache/
  sysinfo 初始化成功；时间恒灰显 → time_svc NTP 桩未接属预期。

**3.2.5 待机触发（I6）**
- 步骤：静止不动 60 秒（默认超时），随后触摸唤醒。
- 预期表现：60s 无输入进入待机（深色底+时钟+提示），任意触摸立即
  恢复原页；串口 `[NAV]: enter standby (idle 60s)` / `standby exited`。
- 排查点：不触发 → 确认启动日志 `standby enabled (timeout=60s)`；
  误触发 → 检查触摸 INT 抖动。

## 4. [WEB] Web 界面测试项

> 前置说明：`/api/sensor`、`/api/net/info`、`/api/time` 当前为 mock
> JSON（[HUB] 批次①未交付，`WEB_API_USE_HUB=0`），故 Dashboard 数值
> 不随真实传感器变化；`/api/wifi/*`、`/api/net/mode`、`/api/ota/*`
> 为真实接口，全部可实测。

### 4.1 访问与入口

**前置条件**：固件已烧录并启动；串口出现 `[WEB] Web server started
on port 80`；PC/手机与设备同网段（STA 模式）或已连接设备热点
（AP 模式，热点名 OVS-xxx 前缀）。

**操作步骤**：
1. 浏览器访问 `http://ovs.local/`（mDNS）或 `http://<设备IP>/`；
   AP 模式下访问 `http://192.168.4.1/`；
2. 确认页面标题"OVS 控制台"、蓝白主题（主蓝 #1E6FFF、白卡片）、
   默认中文（浏览器语言为英文环境时默认英文）；
3. 底部导航依次切换：仪表盘 / 网络 / 固件 / 设置，四页均应正常
   渲染、无白屏。

**curl 端点自测**（替代浏览器逐页点检）：
```bash
curl http://<ip>/api/hello          # {"message":"Hello from ESP32!","status":"ok"}
curl http://<ip>/api/sensor         # {"temp_c":26.5,"humi_p":48.2,"age_ms":1234,"valid":true}（mock）
curl http://<ip>/api/net/info       # {"mode":"ap","ssid":...,"ip":...,"netmask":...,"gw":...,"mac":...,"rssi":...,"switching":false}
curl http://<ip>/api/time           # {"synced":false,"text":"HH:MM"}
curl http://<ip>/i18n/zh-CN.json    # 设备端已部署语言包时返回原文；未部署 404（前端自动回退内置包）
curl http://<ip>/api/ota/status     # {"state":"idle",...,"version":"vX.Y.Z",...}
```

**通过标准**：以上端点均返回合法 JSON（HTTP 200；/i18n 未部署时
404 属预期）；四页面可打开且文案随设置页语言切换即时变化。

**排查点**：页面打不开→看串口 [WEB] 日志与 IP/mDNS；接口 404→确认
固件包含 web_api_sysinfo.c（CMakeLists SRCS）。

### 4.2 Dashboard / Network / Settings

**Dashboard**：
1. 仪表盘页确认时间章、温湿度卡（mock 值 26.5°C/48.2%RH，30s 自动
   刷新，点刷新图标立即更新）、网络信息卡（模式徽标 + SSID/IP/
   掩码/网关/MAC/RSSI 各行）；
2. [HUB] ath30 交付后复测：温湿度应与串口 `[ath30]` 日志一致。

**Network（配网实测）**：
1. "连接状态"卡对照 `/api/wifi/status`（串口 net_mgr 状态一致）；
2. 点"扫描"→ 列表按信号强度降序展示（对照 `curl http://<ip>/api/wifi/scan`）；
3. 点击任一加密网络 → 输入密码 → 确定 → 显示"正在连接…"，约 ≤35s
   内变为"已连接"并展示新 SSID/IP/RSSI；串口预期：`[NET_MGR]`
   配网/切换相关日志、`EVENT_WIFI_GOT_IP`；输错密码时 30s 回退窗口
   后回到原状态（回退保护）；
4. "切换模式"分段按钮：STA↔AP↔OFF 热切换（免重启），切换中显示
   "切换中…"徽标；切 AP 后设备 IP 变为 192.168.4.1，需重连热点再
   访问（页面有提示文案）；串口预期 `[NET_MGR] Mode switched: sta -> ap`；
   curl 对照：`curl -X POST -H "Content-Type: application/json" -d '{"mode":"ap"}' http://<ip>/api/net/mode`。

**Settings**：
1. 语言切换 简体中文↔English 即时生效（导航/卡片/按钮全量切换，
   浏览器标题同步变化），刷新后保持所选语言；
2. 音量/设备名称/待机为占位（显示"即将推出"），不可操作属预期。

### 4.3 OTA 升级实测

**前置条件**：4.1 可访问设备网页；准备合法固件
`build/ovs.bin`（或 ovs_release 产出的 OVSO 容器）；建议保留串口
监控观察全过程。

**操作步骤（网页上传）**：
1. 固件页确认"当前固件"卡显示版本与槽位（对照
   `curl http://<ip>/api/ota/status`）；
2. **非法样例拒绝**：任取一个非固件文件改后缀为 .bin（或首字节非
   0xE9/非 OVSO 的文件）上传 → 应提示"固件头校验失败"，"开始升级"
   按钮保持禁用，设备无任何写入（串口无 `[WEB][OTA] Upload started`）；
   超大文件（>2.5MB，容器 >2.53MB）应提示大小超限；
3. **合法上传**：选择 `build/ovs.bin` → 三项校验全部绿色（.bin/头
   合法/大小）→ 点"开始升级" → 进度条到 100% → "设备重启中"并
   8→1 秒倒计时 → 自动探测 → "升级成功！设备已恢复在线"并显示
   新版本号 → 点"刷新"确认页面加载自新固件；
4. 设备侧确认：串口依次出现 `[WEB][OTA] Upload started: N bytes` →
   `Progress` → `Verified OK (sha256=...)` → `Rebooting into new
   firmware`；重启后 15s 确认窗口内不崩（回滚保护），`/api/ota/status`
   版本号已更新、槽位翻转（0↔1）；
5. curl 对照上传（可选）：
   `curl -X POST --data-binary @build/ovs.bin http://<ip>/api/ota/firmware`。

**通过标准**：非法文件 100% 拒绝且设备无副作用；合法 .bin 与 OVSO
容器均能上传成功并重启进入新固件；升级过程断电/断网后设备仍可
回滚到旧槽启动（回滚保护）。

**排查点**：上传中断→看串口 `[WEB][OTA] Client aborted`；校验失败→
`Verification FAILED`（SHA256/长度问题，检查文件完整性）；重启后
反复回退→确认 15s 内未崩溃（`ota` 确认日志）。

## 5. 已知限制与占位（三方 append-only）

- [HUB] LEVEL 档位数值为占位，待 DX-LR22 手册核实；time_svc NTP 未接
  （synced 恒 false）；lora_tp 真机项需两台设备与手册核实后才可执行；
- [GUI] 待机画面为占位接口（内容后续注入）；聊天页/LoRa 状态角标为
  占位（依赖 lora 集成阶段）；
- [GUI] 2026-09-10：bridge 当前 OVS_BRIDGE_HUB_REAL=0（主页时间=
  开机秒数 mock、温湿度/网络=常量 mock），HUB 交付后置 1 接真；
  中文为 LVGL 内置思源黑体常用字集（约 1000 字），生僻字显示方框
  （全字库自定义字体后续接入 fonts/）；未读角标恒 0 占位（等
  EVENT_LORA_TP_* 接入）；提示音钩子未接（audio_player_play_mem
  通路已就绪，属可选后置项）；阻塞式 audio_module_play() 已标
  deprecated，保留一个版本周期；音频实时性（tasker Little 20ms 喂数）
  与外放音质归真机批次验证；测试文件为裸 PCM（"OPCM" 头）格式，
  mp3/wav 等封装格式不支持（解码挂点留给 codec2 等后续扩展）。
- [WEB] （待 WEB 补充）
- [WEB] 2026-09-10：/api/sensor、/api/net/info、/api/time 为 mock
  JSON（[HUB] 批次①未交付；切换开关 WEB_API_USE_HUB 见
  web_api_sysinfo.c 头注释），Dashboard 数值暂不反映真实传感器；
  /i18n/<lang>.json 白名单当前仅 zh-CN/en-US，设备端未部署语言包时
  返回 404，前端自动回退内置语言包（功能不受影响）；[HUB] 追加新
  语言时需在 web_api_sysinfo.c 白名单表同步追加一行；OTA 页展示的
  "新版本"来自 /api/ota/status（其 version 字段随固件构建版本变化）。
