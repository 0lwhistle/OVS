# 任务协作板（三任务共用共读日志）

> 规则（详见 docs/three_tasks_plan.md §4）：
> 1. 每次会话开始**必读本文件末尾 30 行**；结束**必追加**一条本任务条目；
> 2. 条目格式：`[任务名] 日期 | 状态 | 本会话产出 | 需要别人做的事`；
> 3. 跨任务请求：`REQ[发起方→接收方] 内容/原因/期望`；完成方回 `DONE[任务名]`；
> 4. 接口契约变更先发 `BREAK[任务名]` 说明，不得先斩后奏；
> 5. 共用文件（assets/i18n/*.json、event_bus_types.h）只追加，禁改删他人键/定义；
> 6. 禁止删除或改写他人条目；
> 7. 无板开发：真机测试指导统一写 docs/hardware_test_guide.md，
>    每任务只更新自己章节（[HUB]=§1/§2 [GUI]=§3 [WEB]=§4，§5 append-only）。
>
> 任务名与范围：
> [HUB] 数据中枢 = 统一数据接口(i18n/sensor_cache/time_svc/sysinfo)
>                  + T1 AHT30 收口 + T4 lora_tp（子批次①②③）
> [GUI] 板端体验 = T2 音频升级(audio_module/audio_player)
>                  + LVGL 蓝白主题界面（子批次①②）
> [WEB] Web 界面 = Vue 重构 + 只读 API 路由 + OTA 上传界面

---

[HUB] 2026-09-09 | 计划中 | 尚未开始；建议先交批次①四组件骨架（哪怕桩） | 无
[GUI] 2026-09-09 | 计划中 | 尚未开始；建议先做子批次①音频（不依赖 HUB） | 等 [HUB] 批次①（未交付前 bridge/路由用 mock）
[WEB] 2026-09-09 | 计划中 | 尚未开始 | 等 [HUB] 批次①（未交付前路由回 mock JSON）
[GUI] 2026-09-10 | 批次①进行中 | 声明+产出预告：①event_bus_types.h 已按 I8 追加播放服务事件（追加声明见此）：EVENT_AUDIO_PLAY_STARTED=0x0007 / EVENT_AUDIO_PLAY_PROGRESS=0x0008 / EVENT_AUDIO_PLAY_FAILED=0x0009（新增），EVENT_AUDIO_PLAY_DONE 复用既有 0x0002（原无载荷无使用者，grep 已核实），统一载荷 event_audio_play_t{token,err,duration_ms,position_ms}，进度事件节流 ≥10%（每跨 10% 台阶最多一条）；②ovs.dtb.json 已追加顶层 audio 策略节点（compatible=ovs-audio-policy：sd_pin/volume_default/buf_frames/queue_depth，DTREE 读取+缺省兜底），amplifier 既有 sd_pin 键不动；③audio_module 异步化+audio_player 新组件已实现，ovs_tests 音频门禁全绿（185/185，mock i2s 覆盖 §2.4 全场景）；idf.py build 验证进行中 | 无（全部在 GUI 域内，未动他人文件；audio_player NVS 用命名空间 "audio_player"）
[WEB] 2026-09-10 | 已交付（待固件侧编译确认） | ①Vue 前端全量重构：src/{api,components,composables,i18n,router,stores,views} 模块化 + 自研 hash 路由与轻量 store（未引新依赖）+ 蓝白主题（主蓝 #1E6FFF/白卡片/#F5F7FA 底）；②i18n 双语 zh-CN/en-US，语言包 fetch('/i18n/<lang>.json') 与 LVGL 同源、缺键回退英文再回退键名；键名按契约 I1 扁平大写下划线，附录 A 全量种子键+WEB 扩展键已内置 vue-ui/src/i18n/locales/*.json（schema {lang,ver,strings}）；③四页面：Dashboard（/api/sensor 30s 轮询+手动刷新、/api/net/info、/api/time）、Network（wifi/scan|connect|status + net/mode 界面化）、Firmware（前端三重校验 .bin/≤分区/魔数 0xE9|OVSO → XHR 上传 /api/ota/firmware → 轮询 /api/ota/status → 重启倒计时→探测上线→新版本展示）、Settings（语言切换+占位）；④后端 additive：modules/web/web_api_sysinfo.c 按契约 I5 实现 GET /api/sensor、/api/net/info、/api/time、/i18n/<lang>.json（SPIFFS 白名单读取防穿越），HUB 未交付前路由内 mock JSON（WEB_API_USE_HUB=0，CMake 预留开关+REQUIRES 注释，交付后一键切真）；仅 web.c 追加 2 行 init 调用与 CMakeLists 1 行 SRCS（仿 web_ota 模式）；⑤web_data 已用既有 fs_to_c.py 链路重新生成（125KB<2MB SPIFFS 限额）；⑥vite dev(mock 全页面联调)+vite build 本机通过；本机无 ESP-IDF，idf.py build 未跑——C 侧已做 gcc -fsyntax-only 门禁且为纯 additive | ①[HUB]：批次①交付后翻转 WEB_API_USE_HUB=1 并补 REQUIRES sensor_cache sysinfo time_svc（web_api_sysinfo.c 注释已写明；错误枚举名若非 SENSOR_OK/SYSINFO_OK/TIME_SVC_OK 只需微调对应分支）；②[HUB]：assets/i18n 请以 vue-ui/src/i18n/locales/*.json 为种子合并（含 WEB 域键中英译文）；③项目所有者：构建主机跑一次 mybuild.sh --no-flash 验证固件编译，上板测试按 hardware_test_guide §4
[GUI] 2026-09-10 | 批次①②完成 | ①音频：audio_module 异步化（write_async 环形缓冲+tasker 喂数/set_volume/mute(SD 脚)/drain/abort/完成回调，录音路径未动）+ 新增 audio_player（队列≥3/OPCM 文件播放/audio_decoder_t 挂点 v1 passthrough/音量 NVS "audio_player" 命名空间/token 查询）；事件追加已按前条声明落地；②UI：六层全实装（蓝白主题 #1E6FFF/#F5F7FA/白卡片、ui 控件 5 种、navigator 注册表+栈+I6 navigator_set_standby、home/settings/standby 三页、bridge I1~I4 函数表 mock/真身双开关、presenters 事件驱动刷新）；旧 nav.c/page_home 骨架被取代（nav.h/bridge_time 已删）。验证：ovs_tests 234/234 绿（mock i2s 音频 96 项 + nav_stack 49 项）+ idf.py build 绿（容器 esp-idf v6.0.1 实跑，ovs.bin 余 39%）+ PC 模拟 build 绿且 Xvfb 实跑 8s 无错 + ovs_ui_smoke 17/17（导航/语言热切换/mock 数据）；三层零业务头 grep 自检通过；development_log 顶部与 hardware_test_guide §3 已更新 | 需要 [HUB]：交付 I1~I4 后我在 src/lvgl/CMakeLists.txt 置 OVS_BRIDGE_HUB_REAL=1 接真（现=0 全 mock）；REQ[GUI→HUB] 无新增接口需求，音频事件/audio 节点均在 GUI 域内自闭环
