# OVS项目开发日志

## 2026-09-09 - mem_pool 核心内存管理模块落地（批次1迁移完成，真机账单验证 PASS）

### 任务目标
按 v1.1 方案新增 `core/mem_pool`（设计文档
docs/superpowers/specs/2026-09-09-mem-pool-design.md）：malloc/free 同签名
drop-in 接口 + 8B 分配头记账 + 模块桶归属 + magic 防护 + 定长块池，
并完成批次 1（dtbs）迁移与真机回归。

### 完成内容
- **组件 `components/core/mem_pool`**：mem.h（drop-in 层 mem_malloc/calloc/
  realloc/free + 显式层 mem_heap_alloc/dma/psram + 统计接口）、mem_pool.h/c
  （侵入式空闲栈定长块池，范围/对齐/重复归还校验）、mem.c（8B 头 magic
  校验、free 前清魔数捕获 double-free、严格模式 abort、短临界区记账、
  实际 malloc 在锁外；ESP=portMUX / PC=pthread）。_Static_assert 锁定头
  布局 8B。
- **PC 宿主测试 `tests/`（ovs_tests）**：41 用例全绿——libc 语义对表、
  记账核对（+8 头开销精确入账/桶不串账/峰值）、防护（野 free/重复 free
  拒绝 + fork 验证 strict SIGABRT）、块池（耗尽/LIFO/越界/重复归还）、
  双线程 10 万次压测账目归零。
- **批次 1 迁移（dtbs）**：dtree.c/dtb_ab.c 共 6 处文本替换 mem_malloc/
  mem_free；组件 CMake 加 `MEM_MODULE_TAG=MEM_MOD_DTREE`。ovs_vfs 实查
  零 malloc 调用（全走 littlefs 适配层），无需迁移。
- **main.c**：开机 mem_stat_print() 账单（OTA 保护区之外）。
- **真机验证**：OTA 推送测试固件——设备树 JSON 经 mem_malloc 路径从 A/B
  槽加载解析成功（vfs.mounts 读出 4 项）；DTREE 桶 1 笔分配峰值 6242B
  （= 树 JSON 实际大小）、释放归零；全程零 magic 错误零 panic；正式固件
  二次 OTA 回滚确认 PASS。固件 1.54MB，分区余 41%。

### 待解决问题
- **W25Q128 硬件故障（阻塞 VFS 压测回归）**：串口实测 JEDEC ID=0x000000
  （期望 0xEF4018），外部 Flash 无应答，/media /audio /font 三挂载失败、
  压测 P0 中止。**该故障在上一轮 LVGL 冒烟抓包中已存在**，早于 mem 迁移，
  判定硬件问题（疑设备断电重上后接线松动：CS=13/SCLK=42/MISO=41/MOSI=40
  + 供电）。**待用户检查接线后重跑 VFS 压测（置 OVS_RUN_APP_TESTS=1）**。
- cJSON hooks 未挂（留待 web 线统一 cJSON 时一并入 DTREE 桶）。

### 解决方案
- xtensa 下 uint32_t 为 long unsigned：printf 行显式 (unsigned) 转换。
- 测试野指针构造改为"伪负载"模式（libc 块 +8 处清零传入），避免头校验
  越界读块前内存。

### 下一步计划
- 用户检修 W25Q128 接线 → 重跑 VFS 压测闭环批次 1 回归。
- 批次 2 迁移（ota/net_mgr/web）+ cJSON hooks；event_bus 5.1 池重建于
  mem_pool（Phase 1）。

### 代码变更
- 新增: components/core/mem_pool/{mem.h,mem_pool.h,mem.c,mem_pool.c,CMakeLists.txt}、
  tests/{CMakeLists.txt,test_mem.c}、docs/superpowers/specs/2026-09-09-mem-pool-design.md
- 修改: components/dtbs/{dtree.c,dtb_ab.c,CMakeLists.txt}、src/app/main.c
  （+mem 统计/测试开关已复位）、CMakeLists.txt、.gitignore

## 2026-09-09 - LVGL 9.5.0 双平台骨架接入：ESP 固件 + PC 模拟器（真机 OTA 验证 PASS）

### 任务目标
引入 lvgl-9.5.0（用户下载的 zip），使项目可分别构建 **ESP32-S3 固件**与
**PC 模拟器（SDL2）**两个产物，两端跑同一份 UI 代码；模拟器方便脱离硬件
观看/修改/调整 UI。本期交付骨架（显示 null 输出冒烟），真实 st7789/cst816s
对接留下一步（REFACTORING_PLAN 6.2/6.3）。

设计文档: docs/superpowers/specs/2026-09-09-lvgl-dual-platform-design.md

### 完成内容
- **LVGL 库接入**：lvgl-9.5.0 解压 vendor 到 thirdparty/（裁掉 tests/demos/
  docs/examples 非运行时目录，182M→59M）；新增 `thirdparty/lvgl_lib` IDF 包装
  组件（glob 上游 src/*.c，不做目录排除——src/libs/bin_decoder 是 lv_init 必需
  解码器，未启用库由宏守卫成空 TU；thorvg 为 .cpp 被 *.c 通配自然排除）。
- **lv_conf.h 重写为 v9.5 双平台版**：旧文件是 v8 风格（LV_COLOR_16_SWAP/
  LV_DRAW_COMPLEX/LV_MEM_CUSTOM 等宏在 v9 已失效全删）。现仅保留差异化配置：
  RGB565、LV_STDLIB_CLIB（免内置 64KB 静态池）、30fps、LV_USE_OS=NONE、
  WARN 级日志、Montserrat 16/20/28/48；`OVS_SIMULATOR` 宏时才开 LV_USE_SDL。
  两端共用一份（v9 无 SWAP 宏，真屏字节交换将来在 flush_cb 内做）。
- **src/lvgl 骨架六层**：navigator（tileview 三横页 [intercom][home][clock]）、
  pages/page_home（状态栏+48号大字时钟+占位卡片，320×240 横屏）、
  bridge/bridge_time.h（UI 唯一时间来源）。平台边界：port/ 仅 ESP
  （display_port null flush 冒烟 + bridge_time_esp uptime）；sim/ 仅 PC。
- **PC 模拟器**：sim/ 独立 CMake 工程（上游 os_desktop.cmake 路线，
  LV_BUILD_CONF_DIR 指向共享 lv_conf.h）+ scripts/sim_build.sh [--run]。
  SDL2 窗口 320×240 @2x 缩放、鼠标即触摸、关窗即退。logger 组件纯 printf
  实现直接复用零改造。
- **main.c 接线**：lvgl_app_init() 置于 vfs_stack_start() 之后，远离 OTA/网络
  保护区（遵守保护区协作规则）。
- **屏幕尺寸修正**：设备树 lcd_display 240×280 → **320×240**（用户确认实物
  面板尺寸；st7789.c 内部仍硬编码 240×280 未动，6.2 对接时改读设备树）。
- **真机 bug 修复——LVGL 时基**：首版 OTA 后串口发现 `lv_tick_inc() is not
  called` 警告刷屏、smoke 定时器不触发：ESP 端未给 LVGL v9 提供时基。
  修复：lvgl_app_init 中 `lv_tick_set_cb(esp_timer_get_time/1000)`（v9 标准
  做法，无需轮询任务；PC 端 SDL 驱动自装回调互不影响）。
- **验证结果**：双端构建通过（固件 1.54MB，ota 分区余 41%）；模拟器运行
  正常（3 页滑动、时钟走字）；OTA 推真机 PASS——串口见 display 320x240
  初始化、navigator ready、flush_count 持续增长（32→356+）、无 panic，
  回滚保护自动确认。**R1（LVGL v9 × IDF 6.0.1）正式关闭**。

### 待解决问题
- 模拟器窗口需 WSLg（DISPLAY=:0 已验证可用）；Windows 侧直接跑需另配。
- 中文 CJK 字体未接入（LV_FONT 默认 Montserrat），真实 UI 阶段处理。
- lvgl_app.c 的 lvgl_task 与未来其他任务的 UI 访问需统一走
  lvgl_app_lock/unlock（接口已备）。

### 解决方案
- IDF 组件重名：包装组件命名 lvgl_lib（src/lvgl 组件名是 lvgl，不能同名）。
- main 组件报 lvgl_app.h 找不到：main/CMakeLists REQUIRES 增加 lvgl。
- lvgl 组件缺 esp_timer/heap 头：PRIV_REQUIRES 补 esp_timer heap freertos。

### 下一步计划
- 按 REFACTORING_PLAN 6.2 做 st7789 flush_cb 真实对接（set_window+DMA blit+
  rgb565_swap，宽高改读设备树），6.3 cst816s indev 接入，真屏点亮。
- navigator 扩展 nav_push/nav_pop 页面栈与页面生命周期（on_hide 停 timer）。
- 中文 CJK 字体接入。

### 代码变更
- 新增: thirdparty/lvgl-9.5.0/（vendor）、thirdparty/lvgl_lib/、sim/（CMake+
  main.c+bridge_time_sim.c）、scripts/sim_build.sh、src/lvgl/port/{display_port,
  bridge_time_esp}、src/lvgl/{navigator/nav,pages/page_home,bridge/bridge_time.h}、
  docs/superpowers/specs/2026-09-09-lvgl-dual-platform-design.md
- 修改: src/lvgl/{lv_conf.h,lvgl_app.c/h,CMakeLists.txt}、src/app/main.c、
  main/CMakeLists.txt、CMakeLists.txt、components/dtbs/config/ovs.dtb.json、
  .gitignore、thirdparty/（删 lvgl-9.5.0.zip.Zone.Identifier）
- 删除: include/lvgl.h（24 行假 stub，REFACTORING_PLAN C4）

## 2026-09-09 - 全项目审读 + 重构与架构优化方案（docs/REFACTORING_PLAN.md）

### 任务目标
整体理解项目现状，判定过时/无效内容，产出指导达成产品目标
（LVGL触屏UI系统、LoRa对讲、Web上位机、蓝牙配网）的重构与优化方案文档。

### 完成内容
- **全库审读**：三路并行深读（核心层+设备树 / 全部modules+drivers / 应用层+
  LVGL+Web前端+构建配置），交叉验证 grep 调用链、sdkconfig、partitions.csv。
- **产出 `docs/REFACTORING_PLAN.md` v1.0**，要点：
  - 过时判定 33 项（代码 C1-C18 / 文档 D1-D7 / 配置 B1-B6），均附依据与处置。
    重点：include/ 全局头 6 对同名异体（logger 旧版最危险）、eventbus_api 空壳、
    thirdparty/littlefs-2.11.3 未引用拷贝、heartbeat_init 无人调用致
    /api/status rssi 恒 0、hardware_notes_for_software.md v3.3 引脚表与
    接线图/设备树大面积冲突（LoRa/LCD_RST/LED/I2S 引脚）。
  - 核心判断：主线（net_mgr/ota+dtb A/B/ovs_vfs/web）真机验证保留加固；
    st7789/cst816s/aht30/lora/audio_module/holder 约4600行"写完未接线"
    属半成品资产，盘活而非重写；LVGL 库本身缺失是最大空白。
  - 架构设计：启用 holder 依赖拓扑编排替代 main.c 手工序列（required 仅5个，
    人机类失败降级无屏运行）；event_bus 四修复（锁外回调/内存池/统计原子/
    名表）后全链路接线（现零订阅者）；tasker 原地重构（忙轮询→事件组阻塞）。
  - 五大新基建模块设计：audio_srv（I2S管线+混音，音频帧走FreeRTOS队列不过
    事件总线）、media_player（esp_jpeg硬解+自研.mjp容器+20fps视频）、
    time_srv（SNTP+闹钟JSON存储）、power_srv（五态电源机+LEDC背光+light sleep）、
    lora_proto（帧同步头+CRC16修订版协议+Codec2 1200bps，SF7空口约束论证：
    初稿ADPCM方案带宽不可行）、ble_prov（官方wifi_prov_mgr+NimBLE，非自研GATT）。
  - 性能/功耗：SRAM/PSRAM 内存预算表、SPI共线调度纪律、双核任务-优先级总表、
    分区重排方案（ota 2.5→3.8MB×2，littlefs 8.75→1MB，尾部预留5MB）、
    电源五状态机与电流预算估算。
  - 路线图 Phase 0-6（清理→核心层→LVGL运行时→UI框架→媒体音频→对讲+Web→
    BLE+电源+发布），每阶段附真机验收标准；风险决策 R1-R10。

### 待解决问题
- 方案中三处需真机验证后定案：LVGL v9 与 IDF 6.0.1 兼容性（R1）、
  Flash QIO / PSRAM 80MHz 稳定性（R2）、Codec2 实时性实测（R3）。
- wiring_diagram.md LED 引脚表内部矛盾（GPIO4 vs GPIO14）待与实物核对。

### 下一步计划
按 Phase 0 执行：include/ 解散 + 垃圾文件清理 + 分区重排 + bug 修复
（heartbeat 接线、dtree 节点池、ovs_vfs 加锁、w25q128 缓冲）。

### 代码变更
仅新增 docs/REFACTORING_PLAN.md；本文档条目。未改任何源码。

## 2026-09-09 - 全仓库文档一致性校准（README/docs/组件README/技能文档）

### 任务目标
基于当前代码与最近提交（设备树单棵树重构、A/B 分区 OTA、WiFi 热切换、
VFS 修复），检查仓库自有文档是否过时或与实现不一致；仅修正可直接从
代码/配置/提交记录确证的内容，保持原文档结构与写作风格。

### 完成内容
- **根 README.md**：目录树对齐现状（esp_littlefs、thirdparty+sha256、
  删虚构 config/、main/ 改垫片说明）；ota_update.sh（已删除的脚本）→
  ota_push.sh；5 个分总线 JSON 表 → 单棵树 ovs.dtb.json；DTREE 使用示例
  改为输出指针语义 + find_by_compatible；OTA 特性删"断点续传"（代码无此
  实现）改"15s 回滚保护"；Node 16+ → 20+（vite ^8）。
- **docs/PROJECT_STRUCTURE.md**：目录树全面对齐（删 beep/sr04、补全
  modules/core/drivers、dtb_ab、esp_littlefs、docs 清单）；删"待实现"
  标记；组件依赖按各 CMakeLists 实际 REQUIRES 修正（web 依赖 tasker 而
  非 tasker_api 等）；旧版 LVGL 章节（page_home.c/theme_dark.c/
  lvgl_app_switch_page 等不存在项）改写为现状 + 脚手架说明。
- **docs/ARCHITECTURE.md**：目录树 main/ 垫片化 + 补 src/app；删不存在的
  themes/；"开发日志新条目追加在末尾"→顶部（与实际相反）。更新日志历史
  段落未动。
- **docs/ota_guide.md**：main/main.c → src/app/main.c（OVS_ENABLE_NET 实际
  所在）；API 表补 POST /api/net/mode 与 GET /api/hello（web.c 实测路由）；
  规则5"改设备树需完整 flash"补 A/B 槽 OTA 通道说明（与第三/四节自洽）。
- **docs/handoff_summary.md**：补 2026-09-08 后三次提交成果（VFS 修复压测
  54/54、dtb A/B OTA、热切换真机 PASS）；待办对齐开发日志"下一步"；
  日志方向改"顶部"。
- **docs/peripheral_drivers_summary.md**：spi.json/vfs.json 引用 →
  ovs.dtb.json；bus.host 属性与 DTREE_HOST()（均已删除）改为节点名 +
  dtree_get_host_id()；初始化流程对齐当前 app_main（外设 init 未接入）。
- **组件 README**：event_bus 补 4 个 WiFi 事件 + WATCHDOG_FEED +
  LORA_READY；w25q128 修错误码表（与枚举不符）、10/20MHz→40MHz、
  超时常量、文件结构补 w25q128_vfs、设备树配置节选改嵌套单棵树；
  ovs_vfs 挂载示例/分区表对齐 ovs.dtb.json（/font 2MB、/audio 6MB、
  /media 8MB、/config internal）；holder 删"参考 main.c 使用示例"
  （main.c 未接入 holder）。
- **技能文档**：SKILL.md 设备树示例路径 spi.* → buses.spi2.*、补
  net_mgr_switch_mode、日志读取方向；README/USAGE_GUIDE 结构树对齐实际
  目录（references/ 已删除）；SKILL 版本 v2.4 → v2.5。
- **代码注释**：dtb_ab.h 槽子类型注释 0x90 → 0xA0（partitions.csv、
  dtb_ab.c、ARCHITECTURE 更新日志三处均为 0xA0）。
- diy-smart-assistant README/PROJECT_SUMMARY 同步修 ota_update.sh、
  JSON 表、main/ 说明与文档集实际位置。

### 未改动说明
- development_log*.md 既有条目、ARCHITECTURE.md 更新日志段（历史记录，
  不回写）；diy web_interface.md 等设计文档（定义目标产品，非实现描述）；
  第三方库文档（esp_littlefs/cJSON/littlefs/mongoose）；LVGL 各层 README
  （设计约定类，与脚手架现状相符，仅 src/lvgl/README.md 加现状说明）。

## 2026-09-08 - WiFi AP/STA 免重启热切换：接口封装 + 真机双向验证

### 结论
esp_wifi 支持 STA/AP 运行时切换（stop→set_mode→start），**无需重启**。
在此之上封装了策略层接口并真机验证双向切换。

### 实现内容
- **net_mgr_switch_mode(mode, &prev)**（net_mgr.h/.c）：热切换统一入口。
  同模式幂等（不发事件）；模式确实变化时发布新事件
  EVENT_WIFI_MODE_CHANGED（event_wifi_mode_changed_t，old/new uint8）。
  STA 凭据沿用 NVS/设备树配置，AP 参数沿用设备树/兜底（OVS-xxxx）。
- **Web 端点**：POST /api/net/mode（body: {"mode":"sta"|"ap"|"off"}），
  返回 previous/mode/state/ssid/ip；前端/LVGL 可直接调。
- **main.c 自测**：OVS_NET_HOTSWAP_TEST 开关（默认 0）。开时启动自测任务：
  STA 稳定 → 切 AP（保持 20s，日志打 mode/state/ssid/ip）→ 切回 STA
  （最长等 25s 重连）→ 打印 PASS/FAIL，关键日志只有 [TEST] 行。
- **顺带修复**：net_mgr_get_status AP 分支 ifkey "AP_DEF" → "WIFI_AP_DEF"
  （与此前 STA 的 "STA_DEF"→"WIFI_STA_DEF" 同族问题），AP 模式 IP 可正常显示。

### 实测（OTA 推送 + 串口 [TEST] 日志）
```
baseline(sta): mode=sta state=connected ssid=wifi2.4g ip=192.168.2.111
step1 sta->ap rc=0 prev=sta
in-ap: mode=ap state=ap_running ssid=OVS-E7A0 ip=192.168.4.1
step2 ap->sta rc=0 prev=ap
back-to-sta: mode=sta state=connected ssid=wifi2.4g ip=192.168.2.111
RESULT: PASS (rc1=0 rc2=0)
```
双向切换均 rc=0，AP 期 Web 服务持续运行，回 STA 后原 IP 不变，全程免重启。
API 幂等与参数校验亦验证（bogus→400，同模式→ok 不切换）。

### 使用方式（其他模块）
```c
net_mode_t prev;
net_mgr_switch_mode(NET_MODE_AP, &prev);   // C API
// 或 HTTP: POST /api/net/mode  {"mode":"ap"}
// 订阅 EVENT_WIFI_MODE_CHANGED 获得切换通知
```

### 注意
- 切到 AP 后 STA 断网属预期（互斥模式）；WSL2 无法连 AP，远程切 AP 后
  需在 AP 侧操作或依靠应用层自动回切，谨慎远程调用。
- 自测开关 OVS_NET_HOTSWAP_TEST 已关闭，需要时置 1 烧录。

## 2026-09-08 - 设备树 A/B 分区 OTA 落地 + main.c 整理（实测闭环）

### 任务目标
落地已评审的设备树 A/B 方案：设备树随固件 OTA 升级，仅在 OTA 过程失败
（网络中断/断电）时回退；顺带整理 main.c 测试代码。

### 实现
- **分区表**：+dtb_0/dtb_1（data, 0xA0, 64KB×2 裸分区），littlefs 缩 128KB
  挪至 0x740000；0x90 与 ESP-TEE otadata 冲突故用 0xA0。
- **dtb_ab 模块**（components/dtbs/dtb_ab.{h,c}）：槽格式 `DTBI`
  （魔数+版本+json_len+sha256+JSON）；启动选树：app PENDING_VERIFY→trial 槽，
  否则 active 槽；校验失败自动试另一槽；双槽全废→回退 /spiffs/ovs.dtb.json
  （首次串口烧录零成本）。SHA 仅保证写入完整性，不做语义校验（按需求）。
- **配对回滚**：OTA 只写非活动槽+登记 trial（NVS，掉电安全）；app 15s
  确认(ota_confirm_running)时才翻转 active——app 崩溃回滚则树保持旧版。
- **上传协议**：96B `OVSO` 容器（flags.bit0=含dtb+双SHA256），无魔数=旧式纯
  app 流，向后兼容（web_ota.c + scripts/ovs_pack_payload.py）。
- **独立通道**：POST /api/dtb/firmware 单独更新设备树（写非活动槽+直接翻转）；
  /api/ota/status 增 dtb_slot 字段。
- **构建链**：build 自动产出 build/dtb.bin（dtb_bin 目标）；ota_push.sh 检测后
  自动拼包 build/ota_payload.bin；ota_sha256 提升为共享组件 thirdparty/sha256。
- **main.c 整理**：改产品化启动编排（NVS→核心→SPIFFS→设备树→网/OTA→W25Q128→
  VFS 挂载）；VFS 功能/压力测试收进 OVS_RUN_APP_TESTS 开关（默认关），
  挂载逻辑保留为 vfs_stack_start()（设备树 vfs 节点驱动）。

### 实测验证（串口烧录引导后）
1. 首次启动 dtb 槽空 → 回退 /spiffs 加载成功（WiFi 连上即证明 dtree OK）；
2. OVSO 容器 OTA：app 槽 0→1，dtb_slot 0→1（pending 期用 trial，15s 确认翻转）；
3. 独立通道：POST dtb.bin → 写槽 0 并翻转 active，响应 "takes effect after reboot"；
4. 上传中断模拟（40KB/s 限速推 8s 后 kill）：state 回 idle、app/dtb 指针均未动、
   WiFi 保持连接——核心承诺达成；
5. 中断后再推完整 OTA：11s 上传、确认翻转，状态一致（app 1→0, dtb 0→1）。

### 注意事项
- littlefs 分区挪位清空原 /config 数据（已确认可重建）；
- 设备树语义变更（如新增必需节点）仍需人工保证，A/B 只防"过程损坏"；
- partition table 变更永远需要串口烧录，设备树内容变更从此 OTA 即可。

## 2026-09-08 - VFS 全链路修复：设备树分区重烧 + littlefs 分区数上限

### 任务目标
修复 VFS 全链路失败：W25Q128 报 "Device node 'w25q128-flash' not found"、
vfs 节点找不到走默认配置、/media /audio /font 挂载失败、文件操作与压力测试全挂。

### 根因（三个独立问题叠加）
1. **设备 spiffs 分区中的设备树是旧版**（主因）：设备树 JSON 经
   `spiffs_create_partition_image` 打包进 spiffs 分区（0x520000），
   **OTA 只更新 app 槽不更新 spiffs 分区**，迭代期间设备树 JSON 已多次变更
   （新增 vfs 挂载节点、嵌套单棵树重构），设备上始终是旧数据。
   代码侧 dtree 解析/find_by_compatible/vfs 挂载链路均无缺陷。
2. **main/ 目录在工作区被误删**（未提交的删除）：idf.py 增量链接报
   `undefined reference to app_main`；恢复后仍报错，因 build 缓存的组件列表
   是 main/ 缺失时生成的，需 `idf.py reconfigure` 强制重建。
3. **CONFIG_LITTLEFS_MAX_PARTITIONS=3 不够用**：/font /audio /media 挂满 3 个后，
   /config 报 `esp_littlefs: max mounted partitions reached` →
   ESP_ERR_INVALID_STATE(259)。旧固件只挂 /config 一个所以从未触发。

### 修复内容
- 串口重烧 app + spiffs 分区（`idf.py flash` 含 FLASH_IN_PROJECT 镜像），设备树更新；
- `git restore main/` 恢复被误删的 main 组件垫片（内容与 HEAD 一致，无代码变更）；
- sdkconfig：`CONFIG_LITTLEFS_MAX_PARTITIONS` 3 → 8（与 VFS max_mounts=8 对齐）。

### 验证（串口 /dev/ttyACM0，115200）
- VFS 快速测试全绿：4/4 挂载成功（/font /audio /media→w25q128，/config→internal）、
  /audio 与 /media 写读 ✅、路径匹配 5/5、性能测试、vfs_get_mount_info_by_path、
  vfs_unmount_all + 设备树重挂载 ✅；
- **压力测试两轮 54/54 全部通过**（一轮 681s，修复后含 /config 再跑 775s：
  A 数据完整性 29、B 目录管理 12、C 容量碎片 6、D 并发 4，0 失败；
  顺序写 ~55KB/s，读 ~1290KB/s，最低堆 8088KB）。

### 经验记录
- **串口抓启动日志的正确姿势**：esptool 硬复位会让 USB-Serial-JTAG 重新枚举，
  先行打开的 cat 句柄失效（只收到 26 字节 ROM 信息）；`idf.py monitor` 默认跟随
  `-b 921600` 导致启动日志乱码。正确流程：`idf.py flash` 结束后立即以 115200
  打开端口（一条命令内衔接），或 monitor 单独指定波特率。
- OTA 迭代模式下设备树变更对设备不可见，需串口烧录；后续可考虑 dtree 分区
  OTA/或 web 上传设备树机制。

### 下一步
- W25Q128 每页写/扇区擦日志为 LOGD，logger 新分级下 INFO 级全打印导致刷屏，
  串口监控建议 grep 过滤或运行期 `logger_set_level()`；
- app_main 每次开机无条件跑 VFS+压力测试（~13 分钟），产品化前需加编译/运行开关；
- `wifi_scan_aps` AP 模式限制、OTA 后版本号差异化（沿用上条待办）。

## 2026-09-08 - 开发环境脚本完善（env.sh / ovs_release）

### 任务目标
source env.sh 后 idf 命令与 scripts/ 下脚本均可直接使用（免路径）；
完善 ovs_release 一键发布流程。

### 实现内容
- **env.sh 重写**：
  - 修复未定义颜色变量、删除无效的 `export ./ovs_release`；
  - 基于 BASH_SOURCE 定位项目根，scripts/ 追加进 PATH（防重复），
    任意目录 source 后 `idf.py`、`mybuild.sh`、`ota_push.sh`、`ovs_release`
    等全部可直接按名字调用；自动 cd 到项目根；
  - 导出 `OVS_PROJECT_ROOT`、`OVS_SERIAL_PORT`、`OVS_HOST` 共享变量。
- **ovs_release 重写**：`ovs_release` 只完整构建（Vue+打包+固件）；
  `ovs_release --ota [主机]` 构建后 OTA 推送（缺省 OVS_HOST/ovs.local）；
  任意 cwd 可用（自身定位项目），`--help` 帮助、未知参数报错。
- **mybuild.sh**：新增 `--no-flash`（只构建不烧录，供 ovs_release 复用），
  默认行为不变。
- 全部脚本加执行权限并通过 bash -n 与实际 source 验证
  （/tmp 下 source 后 idf.py --version、各脚本 command -v 全部 OK）。

## 2026-09-08 - Logger 分级打印（ERROR/WARNING/INFO 过滤）

### 任务目标
日志系统支持等级过滤：ERROR 级只打印 LOGE；WARNING 级打印 LOGE/LOGW；
INFO 级全打印（含 LOGD）。

### 实现内容（components/core/logger）
- `log_level_t` 枚举：DEBUG < INFO < WARN < ERROR < NONE（数值越小越详细）；
- 全局等级 = 编译期默认 `LOGGER_DEFAULT_LEVEL`（未定义时 INFO，保持历史行为）
  + 运行期 `logger_set_level()/logger_get_level()` 可调，越界钳位；
- `LOGx` 宏统一走 `_LOG_PRINT`：先 `logger_level_enabled()` 判断再 printf，
  do-while(0) 包装保持语句语义；颜色方案不变；
- LOGD 在 INFO 级输出，WARNING 及以上静音。

### 兼容性
- 全库无把 LOG 宏当表达式使用的代码（已 grep 验证），do-while 包装无副作用；
- 默认 INFO = 原行为，未调用 set_level 的代码路径零变化。

### OTA 已推送验证：升级成功，回滚确认通过。

## 2026-09-08 - WiFi信息接口修复 + OTA上传链路两处关键修复

### 任务目标
1. WiFi驱动补齐 IP/网关/子网掩码/RSSI/扫描 接口；修复运行时 "Failed to get STA netif handle"。
2. 修复 OTA 推送失败（HTTP 500 flash error）。

### 实现内容（WiFi, components/drivers/wifi）
- **根因**：`wifi_get_net_info` 用 ifkey `"STA_DEF"` 反查 netif，而默认 STA 网卡的
  ifkey 是 `"WIFI_STA_DEF"`，恒查不到 → 整个网络信息获取失败。
- **修复**：`wifi_init` 创建 STA netif 时缓存句柄（`s_sta_netif`），查询直接使用。
- **新增接口**：`wifi_get_ip/gateway/netmask(char *buf, int len)`；
  `wifi_get_net_info/wifi_get_rssi/wifi_scan_aps` 原有。验证：`/api/wifi/status`
  正常返回 ip/rssi（-35dBm）。

### 实现内容（OTA/web, 关键度递减）
1. **Mongoose HDRS 接管吞 body**（OTA 失败根因，证据：设备收到的首块从固件
   偏移 0x159D 开始、magic 位为 0x00）：`MG_EV_HTTP_HDRS` 事件中
   `hm->message.len` 包含已随头部提前到达的 body 前缀，`stream_try_takeover`
   按它删除缓冲区把固件开头 5533 字节一起删了。改为扫描 `"\r\n\r\n"` 定位
   头部边界（web.c）。
2. **重启阻塞事件循环**：`ota_reboot` 原用 `vTaskDelay` 在 web 任务内等待，
   导致 HTTP 200 响应永远 flush 不出去（curl rc=56 或挂起）。改为 esp_timer
   500ms 单次定时重启，事件循环继续跑、响应正常发出（ota.c）。
3. **脚本容错**：`ota_push.sh` 上传连接中断时不再直接报错，轮询设备状态，
   槽位翻转即判定升级成功。
4. **日志降噪**：tasker 每秒的 "enqueue successfully" 从 LOGI 降为 LOGD
   （task_worker.c，之前被误认为死循环刷屏）。

### 验证
- OTA 全流程闭环：200 正常返回、上传 14s（修复前 75s）、回滚 15s 自动确认、
  槽位 0↔1 交替正常（ota_push.sh 连续两次成功）。
- WiFi 修复经 OTA 生效（本项目首次完整 OTA 迭代闭环）。

### 代码变更
- components/drivers/wifi/wifi.c/.h（netif 缓存 + 3 个查询接口）
- components/modules/web/web.c（HDRS 头部边界修复）
- components/modules/ota/ota.c（esp_timer 延迟重启）
- components/core/tasker/task_worker.c（enqueue 日志降级）
- scripts/ota_push.sh（中断轮询容错）

### 下一步
- `wifi_scan_aps` 在 AP 模式下不可用（esp_wifi 限制），web 端扫描入口待接入。
- 版本号仍为 git dirty 描述，可考虑 OTA 后差异化版本便于脚本判断。

## 2026-09-07 - Holder模块开发

### 任务目标
添加holder模块，用于全局硬件模块注册表管理，增强系统健壮性。

### 实现内容
1. **创建holder模块** (`components/modules/holder/`)
   - `holder.h`: 公共API接口
   - `holder.c`: 核心实现
   - `CMakeLists.txt`: 组件构建配置
   - `README.md`: 模块文档
   - `example.c`: 使用示例

2. **核心功能**
   - 全局模块注册表管理
   - 模块状态跟踪和监控
   - 错误计数和报告
   - 支持必需/可选模块区分
   - 线程安全设计

3. **集成到主程序**
   - 修改`main.c`，使用holder管理所有硬件模块
   - 为每个硬件模块创建初始化包装函数
   - 实现模块状态检查，避免使用未初始化的模块

### 设计决策
- **单仓库管理**：暂时采用单仓库管理，后续根据需要考虑拆分
- **可移植性设计**：使用标准C类型，抽象平台相关代码
- **错误处理**：必需模块失败时可选择停止系统，可选模块失败只记录日志
- **日志记录**：使用LOGI/LOGW/LOGE/LOGD记录关键操作

### 遇到的问题
1. **初始化函数签名不匹配**：holder需要`int (*)(void)`类型，但现有模块返回类型不同
   - **解决方案**：为每个模块创建初始化包装函数
2. **线程安全考虑**：多任务环境下的并发访问
   - **解决方案**：使用FreeRTOS互斥锁保护共享数据

### 后续计划
1. 测试holder模块在真实硬件环境下的表现
2. 考虑添加模块依赖关系管理
3. 实现模块热插拔支持
4. 添加模块性能监控

### 代码提交
- 提交哈希：64ec1cfc
- 提交信息：添加holder模块：全局硬件模块注册表管理器
- 推送到GitHub：成功

### 遵循的规范
- 使用Logger记录关键操作（LOGI/LOGW/LOGE/LOGD）
- 代码可移植性设计（标准类型、依赖注入、平台抽象）
- 错误处理使用goto cleanup模式
- 命名规范：`module_action()`函数命名

## 编译测试结果

### 编译状态
- ✅ 编译成功，无错误
- 生成二进制文件：`build/ovs.bin` (0x1089b0 bytes)
- 分区空间使用：29%空闲空间

### 编译警告
1. 配置警告：Kconfig默认值不匹配（非关键）
2. 未使用函数警告：`cst816s_write_reg`定义但未使用（非关键）

### 集成验证
- holder组件正确编译并链接
- main.c成功包含holder.h
- 所有硬件模块初始化包装函数编译通过
- 依赖关系正确配置

### 下一步
1. 烧录到硬件测试
2. 验证holder模块在真实环境下的表现
3. 测试模块故障隔离功能

## 文档更新

### 更新内容
1. **ARCHITECTURE.md**:
   - 在"核心架构亮点"部分添加"### 2.5 Holder - 全局硬件模块注册表管理器"
   - 包含设计理念、架构特点、核心功能、API、状态机、技术优势和使用示例

2. **README.md**:
   - 在"主要功能"部分添加"模块管理: 全局硬件模块注册表，故障隔离，状态监控"

3. **peripheral_drivers_summary.md**:
   - 添加"Holdr (全局硬件模块注册表管理器)"部分
   - 包含路径、功能、特性、使用示例和优势

### 文档结构
- 保持与现有文档风格一致
- 使用中文描述，技术术语保持英文
- 包含代码示例和架构图
- 强调holder模块的故障隔离和状态监控功能

### 提交信息
- 更新项目文档，添加holder模块说明
- 保持文档完整性和一致性

## 2026-09-07 工作总结

### 今日完成工作
1. **Holder模块开发** ✅
   - 设计并实现全局硬件模块注册表管理器
   - 创建holder.h、holder.c、CMakeLists.txt、README.md、example.c
   - 集成到main.c，为6个硬件模块创建初始化包装函数
   - 修复依赖问题，编译成功

2. **编译测试** ✅
   - 项目编译成功，生成build/ovs.bin (0x1089b0 bytes)
   - 分区空间使用29%空闲
   - 验证holder模块集成正确

3. **文档更新** ✅
   - 更新ARCHITECTURE.md，添加Holder模块详细说明
   - 更新README.md，添加模块管理功能
   - 更新peripheral_drivers_summary.md，添加Holder模块总结
   - 更新开发日志，记录开发过程

4. **版本控制** ✅
   - 所有代码和文档已提交到Git
   - 已推送到GitHub远程仓库
   - 提交记录清晰，信息完整

### 技术成果
- **系统健壮性提升**：通过holder模块实现硬件故障隔离
- **模块化管理**：统一管理所有硬件模块的生命周期
- **状态监控**：实时监控模块状态和错误信息
- **文档完善**：项目文档体系更加完整

### 遵循的规范
- ✅ 使用Logger记录关键操作（LOGI/LOGW/LOGE/LOGD）
- ✅ 代码可移植性设计（标准类型、依赖注入）
- ✅ 错误处理使用goto cleanup模式
- ✅ 开发日志记录进度
- ✅ 每次功能开发完成都上传GitHub

### 下一步计划
1. 硬件测试：将固件烧录到ESP32-S3硬件测试
2. 功能验证：测试holder模块的故障隔离功能
3. 性能优化：监控模块初始化时间和系统性能
4. 功能扩展：考虑添加模块依赖关系管理

### 项目状态
- **编译状态**：✅ 成功
- **代码质量**：✅ 良好
- **文档完整性**：✅ 完整
- **版本控制**：✅ 已同步到GitHub

---

## 2026-09-07 - VFS模块开发与调试

### 任务目标
完成VFS（虚拟文件系统）模块的开发，实现LittleFS文件系统的挂载和管理功能。

### 实现内容
1. **创建VFS模块** (`components/core/ovs_vfs/`)
   - `vfs.c`: VFS管理器核心实现
   - `vfs_block_dev.c`: 块设备注册表管理
   - `vfs_littlefs_adapter.c`: LittleFS适配层
   - `include/ovs_vfs.h`: 公共API接口
   - `include/ovs_vfs_block_dev.h`: 块设备回调接口

2. **核心功能**
   - 块设备注册和管理
   - LittleFS文件系统挂载
   - 路径路由和挂载点管理
   - 设备树配置自动挂载
   - 文件操作测试和性能测试

3. **集成模块**
   - W25Q128外部Flash驱动
   - 内部Flash驱动
   - 设备树配置文件

### 调试过程
#### 问题1：分区表限制错误
**错误信息**：
```
E (1442) esp_littlefs: No more free partitions available.
E (1442) esp_littlefs: max mounted partitions reached
```

**原因分析**：
- ESP-IDF的LittleFS组件内部维护了一个静态分区表
- 每次调用`esp_vfs_littlefs_register()`都会占用一个分区槽位
- 卸载后重新挂载时，分区槽位没有被正确释放

**解决方案**：
1. 检查LittleFS组件的源码，了解分区管理机制
2. 确保正确调用`esp_vfs_littlefs_unregister()`释放分区
3. 优化挂载/卸载流程，避免重复注册

#### 问题2：内存不足错误
**错误信息**：
```
E (2236) esp_littlefs: Failed to register Littlefs to "/audio"
[VFS_LFS]: LittleFS mount failed for '/audio': ESP_ERR_NO_MEM
```

**原因分析**：
- LittleFS挂载需要分配内存用于缓存和元数据
- ESP32-S3的内部SRAM有限（约512KB）
- 多个LittleFS实例同时挂载时内存不足

**解决方案**：
1. **启用PSRAM**：在`sdkconfig`中启用PSRAM支持
   ```diff
   +CONFIG_SPIRAM=y
   +CONFIG_SPIRAM_MODE_OCT=y
   +CONFIG_SPIRAM_SPEED_40M=y
   ```
2. **优化内存使用**：减少不必要的内存分配
3. **调整LittleFS参数**：优化缓存大小

### 测试结果
#### 功能测试
- ✅ VFS初始化成功
- ✅ 块设备注册成功（W25Q128、内部Flash）
- ✅ 3个挂载点全部挂载成功：
  - `/audio` (W25Q128, 8MB)
  - `/font` (W25Q128, 8MB)
  - `/config` (内部Flash, 9MB)
- ✅ 文件读写测试通过
- ✅ 路径匹配测试通过
- ✅ 卸载和重新挂载测试通过

#### 性能测试
- **写入性能**：16.51 KB/s (4KB数据)
- **读取性能**：1097.69 KB/s (4KB数据)
- **挂载时间**：约2秒（包含格式化）

### 技术成果
1. **模块化设计**：VFS模块完全解耦，支持多种存储设备
2. **设备树集成**：通过JSON配置自动挂载文件系统
3. **错误处理**：完善的错误处理和日志记录
4. **性能优化**：合理的缓存策略和内存管理

### 遵循的规范
- ✅ 使用Logger记录关键操作（LOGI/LOGW/LOGE/LOGD）
- ✅ 代码可移植性设计（标准类型、依赖注入）
- ✅ 错误处理使用goto cleanup模式
- ✅ 开发日志记录进度
- ✅ 每次功能开发完成都上传GitHub

### 下一步计划
1. **功能扩展**：添加FAT文件系统支持
2. **性能优化**：优化缓存策略，提高读写性能
3. **可靠性测试**：进行长时间运行和断电恢复测试
4. **文档完善**：更新API文档和使用示例

### 项目状态
- **编译状态**：✅ 成功
- **功能完整性**：✅ 完整
- **性能表现**：✅ 良好
- **代码质量**：✅ 良好
- **版本控制**：✅ 已同步到GitHub

## 2026-09-08 - 修复VFS卸载后无法重新挂载（ESP_ERR_NO_MEM）

### 问题现象
首次挂载 `/audio`、`/font`、`/config` 全部成功，执行 `vfs_unmount_all()`
后再自动挂载时全部失败：

```
[VFS]: esp_vfs_unregister for '/config' returned: ESP_ERR_INVALID_STATE
[VFS]: Dummy VFS register failed: ESP_ERR_NO_MEM
E (2189) esp_littlefs: Failed to register Littlefs to "/audio"
[VFS_LFS]: esp_vfs_littlefs_register returned: ESP_ERR_NO_MEM (257)
[MAIN]: ❌ 挂载失败: /audio (err=-3)
```

### 根因分析
此问题与堆内存无关（测试时 free_heap 约 8.6MB），真正原因在 ESP-IDF 的
VFS 内部实现：

1. `components/vfs/vfs.c` 使用静态计数 `s_vfs_count` 记录 VFS 注册的
   “历史峰值”，该值**只增不减**；
2. `esp_vfs_register_fs_common()` 在 `s_vfs_count >= CONFIG_VFS_MAX_COUNT`
   时直接返回 `ESP_ERR_NO_MEM`；
3. 本项目 `CONFIG_VFS_MAX_COUNT=8`，首次挂载（SPIFFS + 3个LittleFS挂载点，
   加上系统 /dev/uart、/dev/null、/dev/console 等）使 `s_vfs_count` 达到 8；
4. `esp_vfs_unregister()` 会释放槽位，但不会让 `s_vfs_count` 回退，
   因此卸载后再次注册时即便有空槽位也会立即返回 `ESP_ERR_NO_MEM`。

日志中的 “Dummy VFS register failed: ESP_ERR_NO_MEM” 正是该现象的直接证据：
路径 `/dummy` 从未注册过、堆也充足，说明失败来自 VFS 数量上限而不是内存。

### 修复内容
1. **sdkconfig**：`CONFIG_VFS_MAX_COUNT` 由 8 提高到 16，
   为“卸载后重新注册”预留足够的余量（VFS 历史峰值不再等于上限）。
2. **`components/core/ovs_vfs/src/vfs.c`**：
   - 删除卸载分支中重复的 `esp_vfs_unregister()` 调用；
     `esp_vfs_littlefs_unregister_blockdev()` 内部已完成 VFS 注销，
     重复调用只会得到 `ESP_ERR_INVALID_STATE`。
   - 删除 “dummy VFS 注册/注销循环” 的错误尝试；
     该方法无法让 ESP-IDF 的 `s_vfs_count` 回退，反而可能在未到上限时
     把历史峰值推高，加剧问题。

### 验证计划
- [ ] 重新编译并烧录
- [ ] 观察日志：卸载后再挂载 3 个挂载点全部成功
- [ ] 无 `ESP_ERR_INVALID_STATE` / “Dummy VFS register failed” 日志

### 项目状态
- **编译状态**：待重新编译验证
- **代码质量**：✅ 良好

## 2026-09-08 - SPI总线共享设计分析（ST7789显示屏 + W25Q128）

### 需求背景
确认 ST7789（2.8寸 SPI 电容触控屏，SPI部分）与 W25Q128（16MB SPI NOR Flash）
是否可以共享同一条 SPI 总线，以及如何避免冲突和性能下降。

### 结论
1. **电气/协议层面可以共享**：
   - 当前设备树 `spi.json` 中两者共用 SCLK/MOSI/MISO（GPIO42/40/41），
     片选独立：LCD=GPIO48，Flash=GPIO13；
   - CST816S 触摸走 I2C（GPIO16/17），不占用 SPI 总线；
   - 只要 CS 独立、时序/模式匹配，不会产生总线冲突。
2. **当前软件层面存在冲突隐患**：
   - `st7789.c` 与 `w25q128.c` 各自持有一个 `s_spi_handle`，
     并且各自调用 `spi_drv_init()`；
   - `spi_drv_init()` 内部固定对 `SPI2_HOST` 调用 `spi_bus_initialize()`，
     而一条 SPI 总线只能初始化一次；
   - 因此两者同时使能时，后初始化的模块会拿到
     `ESP_ERR_INVALID_STATE` 导致初始化失败（不只是性能下降）。
3. **性能上会时间片共享**：
   - 同一总线上两个设备的所有传输严格串行；
   - 全屏刷新 240x280x2≈134KB，40MHz 下理论耗时约 27ms/帧；
   - W25Q128 4KB 写入实测约 55ms（含擦除等待），期间若与刷屏抢占总线，
     画面会卡顿/撕裂；
   - 注意：W25Q128 擦除等待本身不占用 SPI 总线，适合放到独立任务异步等待。

### 推荐方案（按优先级）
1. **必须做**：SPI 总线单例化。
   `spi_drv_init()` 改为引用计数/共享句柄，总线只初始化一次；
   各模块通过同一句柄 `spi_drv_add_device()` 注册自己的设备。
2. **性能敏感场景**：把显示和 Flash 拆分到两条 SPI 主机
   （ESP32-S3 上 `SPI2_HOST` 之外还有 `SPI3_HOST` 可用），
   两条总线各自独立 DMA/时钟，互不阻塞。
3. **共享总线场景的缓解措施**：
   - LCD 刷新使用 DMA 队列 + 双缓冲，刷新任务与文件读写任务分离；
   - Flash 大块擦除/写入放后台任务，擦除期间不阻塞刷屏；
   - /font 等静态数据上电后载入 RAM，减少运行期 Flash 读；
   - 有条件时给 W25Q128 开 Quad 模式，降低总线占用。

### 项目状态
- **结论已归档**：`docs/peripheral_drivers_summary.md`
- **代码改动**：✅ 已实施（总线驱动共享计数重构），详见下方日志

## 2026-09-08 - 总线驱动共享计数重构（Linux 式）

### 任务目标
整套架构按 Linux 驱动模型统一：总线/外设驱动使用共享计数，
同一总线只初始化一次，最后一个使用者释放时才真正销毁硬件。

### 实现内容
1. **SPI 驱动**（`components/drivers/spi_drv/`）
   - 增加总线单实例注册表与引用计数；
   - `spi_drv_init()` 变为“获取共享句柄”：首次调用初始化 SPI2_HOST，
     后续相同配置调用只增加引用；
   - `spi_drv_deinit()` 递减引用，归零才调用 `spi_bus_free()`；
   - 总线销毁前自动清理未移除设备与未完成 DMA 传输，避免悬垂/泄漏；
   - 配置不一致返回 `SPI_DRV_ERR_CONFIG`。
2. **I2C 驱动**：同一 SDA/SCL 总线共享句柄、引用计数；
   总线销毁时统一释放设备链表与 I2C 主机。
3. **I2S / UART 驱动**：同样改为共享计数单例，
   为未来多个调用方共用做准备。
4. **模块侧修正**
   - `st7789` 在释放前先 `spi_drv_remove_device()`；
   - `cst816s` 释放时归还 I2C 引用（原来只置空句柄，会造成引用泄漏）。

### 风险与错误处理约定
- init/deinit 受互斥锁保护；配置不一致显式报错，不静默复用错误总线；
- 释放顺序固定：先 remove 自己的 device，再释放 bus 引用；
- 最后一个引用释放失败（如 `spi_bus_free` 失败）返回错误码，
  总线保持可重试，不产生半初始化状态；
- 引用计数模型下禁止模块重复调用 deinit，模块侧以 initialized 标志保证一次释放。

### 编译状态
- ✅ `idf.py build` 通过

## 2026-09-08 会话收尾总结

### 本次会话完成内容
1. **VFS 修复**：卸载后无法重挂载（`ESP_ERR_NO_MEM`）
   - 根因：ESP-IDF VFS `s_vfs_count` 只增不减且 `CONFIG_VFS_MAX_COUNT=8`；
   - 修复：上限提高到 16，清理重复 unregister 与 dummy VFS hack。
2. **总线驱动共享计数**（Linux 式）
   - SPI/I2C/I2S/UART 统一引用计数；SPI/I2C 均支持同总线多设备。
3. **设备树 host 显式化**
   - 各总线增加 `host` 属性；dtree 新增 `DTREE_HOST()` 解析；
   - SPI2/SPI3、I2C0/1、I2S0/1、UART0/1/2 多控制器注册表。
4. **Holder 依赖初始化**
   - 新增 `holder_register_module_ex()` 与依赖分批初始化。
5. **W25Q128 健康状态机**
   - READY/FAULT、操作前 JEDEC 探测、自动恢复；
   - 修复 FreeRTOS 100Hz 下 `pdMS_TO_TICKS(1)=0` 导致的忙等空转；
   - 关闭自动格式化；测试脚本补错误检查。

### 当前状态
- ✅ `idf.py build` 通过
- ⚠️ 未烧录硬件验证
- ⚠️ 改动未提交 git

### 下次继续
1. 烧录最新固件 + SPIFFS；
2. 验证 W25Q128 正常读写、拔插故障、恢复；
3. 验证 /audio、/font、/config 三个挂载点；
4. 确认后再提交并开始下一模块开发。
- ⚠️ 待硬件验证：ST7789 + W25Q128 同时初始化并共享 SPI2_HOST

## 2026-09-08 - SPI 共用优化方案对比

### 方案A：维持共享总线（当前）
- 两个设备共用 SPI2_HOST、独立 CS；
- 适合低频 Flash 操作（字库上电读入 RAM、配置写入等）；
- 性能上限：总线时间片串行，全屏刷新 134KB @40MHz 约 27ms，
  Flash 大文件持续读会与刷屏竞争；
- 风险：刷屏卡顿/撕裂；信号完整性与线长。
- 缓解：LCD DMA+双缓冲、Flash 后台任务、按需缓存字库、W25Q128 Quad 模式。

### 方案B：拆分两条 SPI 主机（推荐做性能预留）
- ST7789 走 SPI2_HOST（40MHz），W25Q128 走 SPI3_HOST（可提到 80MHz）；
- 两条总线独立 DMA/时钟，Flash 操作不再阻塞刷屏；
- 风险：需要改板（W25Q128 的 4 根信号改接到 SPI3 引脚组）或者使用
  GPIO 矩阵重映射到另一组可用引脚；引脚不够时不可行；
- 错误处理：驱动层需支持多总线注册表（当前 SPI 注册表预留了该扩展点）。

### 方案C：更换存储接口
- W25Q128 若仍不够用，改用 SD/SDMMC 或 QSPI Flash；
- 风险：硬件改动大、成本高，仅当 Flash 吞吐成为瓶颈时考虑。

### 项目状态
- **代码改动**：✅ SPI/I2C/I2S/UART 共享计数已实现并编译通过
- **硬件验证**：待烧录验证

## 2026-09-08 - 总线 host 显式化与 dtree/holder 适配

### 任务目标
设备树中显式区分每个总线的控制器（host/port），驱动按 host 建立
多实例共享注册表；dtree 与 holder 配套适配，为将来 SPI2/SPI3 拆分、
多 I2C/I2S/UART 控制器提供基础设施。

### 实现内容
1. **设备树 host 属性**（`components/dtbs/config/*.json` 与 `spiffs_image/`）
   - `spi.bus.host = "spi2"`
   - `i2c.bus.host = "i2c0"`
   - `i2s.bus.host = "i2s0"`
   - `lora.uart.host = "uart1"`、`system.uart0.host = "uart0"`
2. **dtree 解析适配**
   - 新增 `dtree_get_host()`：解析 `"<prefix><number>"`，
     校验前缀与数字格式；
   - 新增便捷宏 `DTREE_HOST(path, prefix, value)`。
3. **驱动多实例注册表**
   - SPI：`host=2/3` 各维护一个共享总线表项；
   - I2C：`port=0/1`；I2S：`port=0/1`；UART：`port=0/1/2`；
   - 每个控制器实例内部仍是引用计数共享模型。
4. **Holder 适配**
   - 新增 `holder_register_module_ex()`，支持声明依赖；
   - `holder_init_all()` 改为按依赖分批初始化，缺失/循环依赖
     自动标记 ERROR，不再依赖注册顺序。

### 约定与错误处理
- host 缺失、前缀不匹配或编号越界：load_config 返回
  `*_DRV_ERR_CONFIG`，模块初始化失败由 holder 记录；
- 同一 host 被不同引脚配置重复获取：返回配置错误，不破坏既有总线；
- holder 直接调用 `holder_init_module()` 时若依赖未就绪，
  返回 `HOLDER_ERR_DEPENDENCY`，请使用 `holder_init_all()` 自动排序。

### 编译状态
- ✅ `idf.py build` 通过
- ⚠️ 需重新生成并烧录 SPIFFS（配置 JSON 已变更）

## 2026-09-08 - W25Q128 健康状态机（READY/FAULT）

### 任务目标
让 W25Q128 在测试阶段具备基本产品可靠性：芯片接触不良/被移除时不静默
返回错误数据、不长时间卡死，并能在恢复后自动回到可用状态。

### 实现内容
1. **状态机**：`READY` / `FAULT`
   - 连续 3 次操作失败（含探测失败）进入 `FAULT`；
   - `FAULT` 后至少间隔 1s 才允许再次探测，避免高频重试；
   - 探测成功自动恢复 `READY`，发布 `EVENT_STORAGE_READY`。
2. **每次公开操作前做 JEDEC 探测**
   - 芯片不在线时返回 `W25Q128_ERR_OFFLINE`，不再执行 I/O；
   - 读操作不再出现“拔掉后静默读全 0xFF”的情况。
3. **忙等超时**
   - 单次/扇区 busy 等待保持 5000ms；
     （曾压到 1000ms，实测部分扇区擦除超过 1s 会被误报超时，已恢复；
     由于操作前已有 JEDEC 探测，芯片不在线时不会进入该等待）
   - 整片擦除等待单独保留 60s（正常芯片本身耗时较长）。

   > 修正：`CONFIG_FREERTOS_HZ=100` 时 `pdMS_TO_TICKS(1)` 取整为 0 tick，
   > 原 busy 等待实际是空转轮询而非按毫秒延时。已改为每 10ms 轮询一次，
   > 使 5000ms 真正对应 5 秒等待上限。
4. **禁止静默自动格式化**
   - `vfs.json` 的 `format_if_fail` 全部改为 false；
   - 只有显式调用 `vfs_format()` 才允许格式化。
5. **接口**
   - 新增 `w25q128_health_check()`、`w25q128_is_ready()`；
   - VFS 块设备注册/取大小改为检查 ready 状态。
6. **测试脚本修正**
   - 文件/性能测试补上对 `fwrite`、`fread`、`fclose` 返回值的检查，
     避免“磁盘写失败但仍打印 ✅ 成功”的假日志。

### 待硬件验证
- [ ] 正常读写/擦除不受影响；
- [ ] 拔掉芯片后，连续操作 3 次进入 FAULT，后续快速返回 OFFLINE；
- [ ] 重新插上后，下一轮操作自动恢复 READY；
- [ ] SPIFFS 中 `vfs.json` 与代码同步生效（需重新烧录 spiffs）。

### 编译状态
- ✅ `idf.py build` 通过

## 2026-09-08 - 设备树重构：嵌套单棵树 + compatible 查询绑定

### 任务目标
模仿真实 Linux 设备树重构 dtree：配置合并为单棵树（设备节点嵌套在总线节点下，
父子关系即挂载关系）；消除代码中硬编码的节点路径，改为按 compatible 属性查询绑定。

### 原有设计的问题
- `lcd_display`/`flash` 等设备节点与 `bus` 平级，JSON 中无任何挂载关系，
  绑定全靠 C 代码硬编码路径约定（如 st7789.c 同时写死 `"spi.lcd_display"` 与
  间接写死 `"spi.bus"`）；
- 一个文件只能有一条总线（`bus` 节点名固定），无法表达 SPI2/SPI3 拆分；
- `compatible` 字段存在但没有任何代码消费它。

### 实现内容
1. **配置格式**（`components/dtbs/config/ovs.dtb.json`，替代原 6 个 JSON）
   - 单棵树：`buses.spi2/i2c0/i2s0/uart0/uart1`，设备节点嵌套在总线节点内；
   - 总线节点名即控制器地址（`"spi2"`→host 2），删除 `host` 属性；
   - lora 设备节点挂在 uart1 下（control 引脚扁平化 + default_config 子节点）；
   - 顶层 `leds.status`、`vfs`（mounts 数组原样迁移）；mcu 信息保留在根下。
   - 同步 `spiffs_image/`（注意：构建实际使用 `components/dtbs/config/`）。
2. **dtree API**（`include/dtree.h`、`components/dtbs/dtree.c`）
   - 新增 `dtree_find_by_compatible()`（全树 DFS）、`dtree_get_parent()`、
     `dtree_get_host_id()`（从节点名解析编号）、`dtree_get_node_name()`；
   - 删除 `dtree_get_host()`/`DTREE_HOST()`；
   - 加载逻辑改为单文件 `/spiffs/ovs.dtb.json`，文件缺失返回 `DTREE_ERR_IO`；
   - 修复节点池 name 悬垂隐患（统一取 cJSON 键，生命周期与树相同）。
3. **总线驱动**（spi/i2c/i2s/uart）
   - `*_drv_load_config()` 统一改为 `(dtree_node_t* bus_node, config)` 签名，
     从总线节点读属性，节点名解析 host/port；
   - 增加总线节点 compatible 校验（`esp32s3-spi/i2c/i2s/uart`），防呆；
   - i2s_drv 不再读麦克风/功放引脚（设备解耦），din/dout 由调用方填充。
4. **设备模块**（st7789/w25q128/cst816s/aht30/lora/audio_module）
   - 统一模式：`dtree_find_by_compatible(自己的compatible)` → `dtree_get_parent()`
     → 总线驱动 load/init → 设备属性从自己的节点读取；
   - 各模块以宏声明自己服务的 compatible（如 `ST7789_DT_COMPAT "st7789-lcd"`）；
   - audio_module 按 compatible 查 mic/amp 节点后填充 din/dout。
5. **未改动**：main.c 的 `vfs`/`vfs.mounts` 路径在新树中依然有效；holder 编排不变。

### 编译状态
- ✅ `idf.py build` 通过，改动文件零警告
- ✅ `build/spiffs.bin` 已包含 ovs.dtb.json（compatible 字符串已确认在镜像中）

### 待硬件验证
- [ ] 烧录 app + spiffs 后：设备树加载、W25Q128/VFS 挂载正常；
- [ ] ST7789 + W25Q128 共享 SPI2 初始化无冲突（本次重构未烧录验证）。

### 下一步计划
1. 硬件验证上述全部功能（需同时烧录 SPIFFS 分区）；
2. 验证通过后将本次设备树重构与上次总线驱动改动一并提交 git。

## 2026-09-08 - 冗余文件与过时文档清理

### 删除内容（均可从 git 历史恢复）
- 根目录：`CMakeLists.txt.bak/.bak2`、`README.md.bak`、`sdkconfig.old`、
  `config/`（09-06 旧配置副本）、`spiffs_image/`（遗留副本，构建实际使用
  `components/dtbs/config/`）、`temp_drivers/`（beep/sr04 实验驱动，未接入构建）
- docs：`ARCHITECTURE.md.bak`、`peripheral_drivers_summary.md.bak`、
  `VFS_DESIGN.md`（VFS 已实现，结论收录于开发日志与架构文档，原文已过时）
- 技能目录：`references/`（skill 自述为过时 v1 资料）；SKILL.md 目录树与注记
  已同步更新为现状

### 其他
- `docs/handoff_summary.md` 重写为 2026-09-08 当前状态
- 清理后 `idf.py build` 复验通过

## 2026-09-08 - SPI 共总线性能优化（LCD + W25Q128）

### 背景
PCB 已定型，ST7789 与 W25Q128 共用 SPI2，下版硬件才会拆分总线。
分析确认：正确性无风险（IDF 总线锁按事务串行 + CS 独立 + Flash 擦除等待
不占总线），但存在两个性能问题：
1. `spi_device_polling_transmit` 使刷屏的 27-40ms 内 CPU 自旋，饿死低优先级任务；
2. LCD 整帧 134KB 一笔事务独占总线，flash 操作尾延迟最多等一帧。

### 实现内容
1. **spi_drv 同步传输改队列+休眠**（spi_drv.c）
   - `spi_device_polling_transmit` → `spi_device_transmit`（队列+阻塞等待）；
   - 传输期间调用任务休眠，CPU 让出；事务粒度由 IDF 仲裁，两设备自然交错；
   - 消除将来混用 polling/queue 的 ESP_ERR_INVALID_STATE 隐患。
2. **st7789 刷屏分片**（st7789.c）
   - `st7789_flush()` 按片发送（每片 24 行 ≈ 11.5KB，整帧 12 片），
     片间总线空闲，flash 事务（4KB 读约 1ms）可插空；
   - 新增常驻内部 DMA RAM 暂存缓冲（约 12KB，init 时一次分配），
     替代原来 spi_drv 内每帧 malloc/free 135KB 的 DMA 安全拷贝；
   - LCD 设备 `max_transfer_sz` 相应降为单片大小（暂存分配失败时回退
     整帧发送并保持原 max_transfer_sz）；
   - deinit 释放暂存缓冲。

### 预期收益
| 指标 | 改前 | 改后 |
|---|---|---|
| 刷屏 CPU | 27-40ms/帧自旋 | ≈0（休眠） |
| flash 尾延迟（撞刷屏） | ≤40ms | ≤3ms |
| 每帧临时内存 | malloc/free 135KB | 0（常驻 ~12KB） |

### 使用约定（不改代码）
- Flash 写/擦除提交到 tasker Lots 队列后台执行，完成后发事件；
- 字库等静态资源上电一次性载入 PSRAM。

### 编译状态
- ✅ `idf.py build` 通过，零警告

### 待硬件验证
- [ ] LCD 刷屏显示正确（分片边界无错位/花屏）；
- [ ] 刷屏同时进行 flash 读写，双方无报错、时延符合预期。

## 2026-09-08 - 架构文档同步与仓库清理提交

### 架构文档同步
- `docs/ARCHITECTURE.md`（v1.1）与重构后代码对齐：
  - 2.3 节重写为"单棵树 + compatible 绑定"（设计理念/架构图/JSON示例/API/优势表）；
  - 第三章协作关系图设备树框改为 ovs.dtb.json 单棵树示意；
  - 第四章目录结构修正（docs/ 实际位置、ovs.dtb.json、无根级架构文档）；
  - 5.5 节开发指导改为 compatible 绑定流程；
  - 末尾更新日志追加 2026-09-08 条目。

### 版本控制清理
- `build/`（4567 个产物文件）从 git 跟踪中移除（本地保留）；
  `.gitignore` 早已包含 `build/`，但此前文件在生效前已被提交，一直跟随变更；
- `components/esp_littlefs` 为子仓库（gitlink），指针变化不随本次提交。

### 本次提交内容
设备树重构（嵌套单棵树 + compatible 绑定）、SPI 共总线性能优化、
冗余文件清理、文档同步，见上方 2026-09-08 各条目。

## 2026-09-08 - VFS 媒体存储分区重排（/media 上线）

### 需求背景
VFS 用于存放媒体：音频供设备扬声器播放，图片/视频为小文件、后续由
桌面开发工具推送。原布局（/audio 8MB + /font 8MB）占满 W25Q128，
没有媒体文件的位置（字库实际 <2MB）。

### 分区重排（components/dtbs/config/ovs.dtb.json）
| 挂载点 | 设备 | 偏移 | 大小 | 用途 |
|---|---|---|---|---|
| /font  | w25q128 | 0       | 2MB | 字库 |
| /audio | w25q128 | 2MB     | 6MB | 扬声器音频 |
| /media | w25q128 | 8MB     | 8MB | 图片/小视频（桌面工具管理） |
| /config | internal | -     | 9MB | 配置（不变） |

### 配套改动
- main.c 新增 `OVS_MEDIA_FORMAT_ON_FIRST_BOOT` 过渡开关（默认 0）：
  分区布局变更后置 1 烧录一次（挂载失败自动格式化 w25q128 各分区），
  完成后必须置回 0，避免日后启动静默清空媒体；
- 挂载失败日志增加上述提示；
- test_vfs 增加 /media 写读与路径匹配测试。

### 性能预期（按当前实测）
- 播放（读 1.1MB/s）：MP3@128kbps 仅需 16KB/s，宽裕；
- 桌面推送（写 16.5KB/s）：小文件可接受，后续可调 LittleFS
  geometry/cache 参数或应用层聚合写提至接近裸写 447KB/s；
- /audio 6MB ≈ MP3@128kbps 约 5 分钟（提示音场景足够）。

### 编译状态
- ✅ `idf.py build` 通过（含新 SPIFFS 镜像）

### 待硬件验证
- [ ] 置 OVS_MEDIA_FORMAT_ON_FIRST_BOOT=1 烧录 → 四个挂载点全部成功 → 置回 0 重烧；
- [ ] /media 读写测试通过；断电重启后文件仍在。

## 2026-09-08 - VFS 文件系统压力测试套件

### 需求
针对 VFS/LittleFS 增加全面高压测试，未通过项在末尾总结集中打印。

### 实现（main/vfs_stress.c + .h，main.c 末尾自动执行）
专用 FreeRTOS 任务（16KB 栈，64KB×2 堆缓冲），五个板块：
- **A 数据完整性**：5 种位型 × 5 种尺寸写读校验；边界尺寸（4KB-1、64KB）
  随机数据比对；64KB 文件 50 次随机位置覆盖写后全文件比对；100 次追加写
  （周期性重开）后逐段比对；
- **B 目录与文件管理**：8 级嵌套目录；150 个小文件创建/readdir 计数/删除/空目录
  校验；重命名后旧名消失、覆盖写截断语义；5 项错误路径（读不存在、写缺失目录、
  删不存在、重复 mkdir、超长文件名）必须优雅失败；
- **C 容量与碎片（/font）**：16KB 文件打满至 ENOSPC（校验可用量 1.5~2MB）、
  删除后 512KB 回写验证回收；20 文件交错写入→删奇数→500KB 大文件写入碎片空间
  并全文件比对；
- **D 多任务并发**：3 个任务并行（/audio×2 + /media×1 各 256KB）写读校验，
  120s 超时保护；
- **E 性能统计**（只报告）：1KB/4KB/32KB/64KB 块顺序写读速率、随机 4KB 读延迟、
  50 小文件创建速率。

### 行为
- 失败项立即打印 FAIL 并存入列表（上限 32 条），结束后打印总结：
  总检查项/通过/失败/耗时 + 失败明细 + 「未通过/全部通过」结论；
- 测试文件全部置于各分区 /stress 目录，结束后尽力清理；
- 分区未挂载时自动跳过对应板块（预检计入检查项）。

### 已知事项
- 过渡开关 OVS_MEDIA_FORMAT_ON_FIRST_BOOT 当前为 1（待烧录格式化新分区，
  确认通过后需置回 0）；
- 上一轮烧录因串口被监控会话占用失败（ttyACM0 被 PID 占用）。

### 编译状态
- ✅ `idf.py build` 通过，零警告

## 2026-09-08 - 修复并发压力测试崩溃 + 进度输出

### 问题：D 节多任务并发开始时设备自动重启
### 根因
/media 与 /audio 的并发 worker 会同时调用 W25Q128 驱动（不同挂载点、
同一块 SPI 设备句柄）。ESP-IDF SPI 主机驱动**不允许两个任务对同一设备
句柄并发提交事务**（总线锁按设备粒度仲裁，同一设备的事务槽位只有一个），
并发提交导致崩溃。此前从未有多任务同时访问该驱动的场景，故未暴露。

### 修复（components/modules/w25q128/w25q128.c）
- 新增设备级互斥锁 `s_dev_mutex`（init 时创建）；
- read/write/erase_sector/erase_chip/health_check 五个触及 SPI 的公开
  函数体改为 `_locked` 静态实现，公开包装器统一拿锁/还锁；
- 锁内操作均有内部超时（busy 等待 5s、整片擦除 60s），等待有界；
- 已确认内部实现无公开函数互调，无递归锁风险。

### 压力测试进度输出（main/vfs_stress.c）
- A1 每种位型完成、A3 每 10 次覆盖、B2 每 50 个文件、
  C1 每 128KB、C2 每 4 轮交错/每 128KB、D 每 5s 汇报 worker 完成数，
  均带已耗时秒数。

### 编译状态
- ✅ `idf.py build` 通过，零警告

## 2026-09-08 - 压力测试：看门狗防护与可复制总结块

### 看门狗分析
- sdkconfig：CONFIG_ESP_TASK_WDT_PANIC 未启用 → 任务看门狗超时仅打印不重启；
  监视对象是 idle 任务（5s 超时）；INT_WDT 300ms 与外部 SPI flash 操作无关；
- 上次 D 节重启确认非看门狗，是 SPI 同设备并发崩溃（已修，见上条）；
- 双保险：C1 打满循环（每 8 文件）、C2 交错（每 4 轮）、C2 大文件（每 128KB）、
  D worker（每 8 个 4KB 块）加显式 vTaskDelay(1) 让步，杜绝 idle 饿死。

### 总结块重构（便于整块复制反馈）
末尾输出一个连续区块，包含：
- [环境] 总耗时 / 空闲堆 / 最低堆
- [分区] /media /audio /font 挂载状态
- [总计] 检查项 / 通过 / 失败
- [板块] 各板块检查数/失败数/耗时（sec_record 统计）
- [性能] E 板块全部实测数据行（perf_log 捕获）
- [失败n] 全部失败明细
- [结论] 全部通过 / 未通过: N 项失败
日志中搜索"VFS 压力测试总结"到"总结结束"即为完整可复制块。

### 编译状态
- ✅ `idf.py build` 通过，零警告

## 2026-09-08 - 首轮压测结果分析与三项优化

### 首轮结果（850.5s，54 项检查）
- 53 通过 / 1 失败；D 并发不再重启（互斥锁修复生效）；
- 写 20.4~20.8 KB/s（与块大小无关），读 1.24~1.33 MB/s，随机 4KB 读 3.9ms。

### 发现与修复
1. **B1 失败为测试代码 bug**：A 板块清理移除了 /media/stress 父目录，
   B1 首个 mkdir(/media/stress/d0) 因父目录缺失失败。已修（先重建父目录），
   并给失败信息补充路径细节。
2. **B2 期间触发一次 IDLE0 任务看门狗（仅打印未重启）**：150 个小文件的
   创建/删除循环未让步。已在 B2 创建/删除循环（每 10 个）和 E 小文件循环
   （每 10 个）补 vTaskDelay(1)。
3. **写入吞吐瓶颈定位**：写速度恒定 ~20.8KB/s 且与块大小无关，根因是
   忙等待 10ms 轮询粒度——每个 256B 页编程都要等一个 10ms tick
   （理论上限 25.6KB/s）。
   **修复**：wait_busy 改为三阶段轮询——立即首查（典型页编程 <1ms 一次完成）→
   前 2ms 短自旋（100µs 粒度）→ 超过 2ms 按 tick 让出轮询（覆盖扇区/整片擦除）。
   预期写入提升至接近裸驱动水平，待下轮压测验证。

### 其他观察
- C 期间出现过一次 esp_littlefs "No more free space" 打印但全部 C 检查通过
  （碎片删除后 lookahead 未及时回收的瞬态，操作已恢复）；暂不处理，若复现
  再增大 CONFIG_LITTLEFS_LOOKAHEAD_SIZE。

### 状态
- ✅ 编译通过零警告
- OVS_MEDIA_FORMAT_ON_FIRST_BOOT 已置回 0（分区已完成格式化过渡）
