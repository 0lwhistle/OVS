# 开发日志 —— 网络与 OTA 模块线

> 本文件为 OTA/网络模块开发的独立日志（与 W25Q128 测试线分开记录，避免并行冲突）。
> 待两条线合并时可整段并入 docs/development_log.md。
> 记录人：ZCode（OTA+网络线）；格式沿用主开发日志条目结构。

## 2026-09-08 - OTA 流式重构 + net_mgr 网络管理器（开发期 OTA 迭代通道）

### 任务目标
- 完善 OTA 模块与网络模块的模块化设计与实现；
- 留出蓝牙配网 provider 接口、Web 前端（上位机）接口；
- 开发期效果：`idf.py build` 后一条命令把固件 OTA 推上设备，替代串口烧录。

### 整体架构（已与使用者确认）
```
main.c (#if OVS_ENABLE_NET 条件接入)
 └─ net_mgr (新模块) ── STA/AP 状态机、NVS 凭据、配网 provider 注册、事件发布
     ├─ drivers/wifi ── 扩展 AP 模式；STA 不再硬编码连接
     ├─ web (重构) ── 路由注册表 + Mongoose 7.21 流式上传 + 静态/WS 保留
     │    └─ web_ota.c ── OTA 上传/状态端点（web 模块内置）
     └─ ota (重构) ── 流式直写、无队列、SHA256、回滚确认
```

### 完成内容

**1. net_mgr（新增 components/modules/net_mgr/）**
- `net_types.h`：可移植错误码 `net_err_t`；
- `net_provision.h`：配网通道 provider 接口（name/start/stop）+ 凭据统一提交
  `net_provision_submit()`。web 通道已注册；蓝牙模块实现后调用
  `net_provision_register()` 即可接入，无需改 net_mgr 内部；
- `net_mgr.h/.c`：STA/AP 双模式状态机（不共存，互斥切换）、凭据 NVS 持久化
  （namespace `net_mgr`，首次启动用 wifi.h 出厂默认播种）、断线由驱动无限自动
  重连、配网失败 35s 回退 NVS、状态快照 `net_mgr_get_status()`（供 Web API 与
  未来 LVGL 网络配置页）、事件发布 EVENT_WIFI_GOT_IP/CONNECTED/DISCONNECTED/
  AP_STARTED/SWITCH_*（event_bus_types.h 新增 AP_STARTED/AP_STOPPED 两事件）；
- mDNS：主机名 `ovs`（ovs.local），_http._tcp 服务（espressif/mdns 1.12.0，
  经 idf_component.yml 管理锁定）。

**2. wifi 驱动扩展（components/drivers/wifi/）**
- `wifi_init()` 改为仅准备协议栈（STA+AP netif、事件回调），不再硬编码连接；
- 新增 `wifi_start_sta()` / `wifi_start_ap(ssid,pass,ch,max)` / `wifi_stop()` /
  `wifi_get_mode()`；
- `wifi_restart_sta()` 轻量化：不再销毁 netif/event loop（修复了热切换会
  摧毁其他组件事件注册的问题），改为 disconnect→stop→set_config→start。

**3. OTA 重构（components/modules/ota/）**
- 删除 PSRAM 队列 + 后台写任务模型，改为**流式直写**：`ota_begin/ota_write/
  ota_end/ota_abort`，内存占用从整包缓冲降到常数级，阻塞写即 TCP 背压；
- 删除自定义尾部签名协议（OTA_MAGIC/sign_firmware.py 依赖）：完整性由
  `esp_ota_end` 标准 IDF 镜像校验保证；接收过程流式计算 SHA256
  （内置公有领域实现 `ota_sha256.c`，因 IDF v6 的 mbedTLS 4.x 已把
  mbedtls/sha256.h 移入 private，且自研实现零依赖可移植），DONE 态经
  `/api/ota/status` 暴露供上位机比对；
- 回滚保护：sdkconfig 启用 CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE；
  `ota_init()` 启动 15s 一次性定时器，稳定运行后
  `esp_ota_mark_app_valid_cancel_rollback()`，坏固件启动即崩时 bootloader
  自动回退旧槽；
- 公开 API 全部可移植类型（ota_err_t/ota_status_t），esp_ota_ops 隔离在 .c。

**4. Web 重构（components/modules/web/）**
- `web.h`：路由注册表 API——`web_register_route()`（精确路由）与
  `web_register_stream_route()`（流式大 body 路由）；模块化：各功能端点
  自行注册，web 核心不认识具体业务；
- 流式上传适配 **Mongoose 7.21**（注意：7.16+ 已删除 MG_EV_HTTP_CHUNK）：
  采用 MG_EV_HTTP_HDRS + 消费 recv 缓冲触发 mongoose detach 协议解析器，
  之后以裸 MG_EV_READ 逐批交付，配合 mg_iobuf_del 排空，内存占用与文件
  大小无关，MG_MAX_RECV_SIZE(1MB) 不再是上传上限；
- 端点：
  - `POST /api/ota/firmware`（流式上传；`/ota/update` 旧路径同 handler）
  - `GET /api/ota/status`（状态/进度/SHA256/槽位/版本/pending_verify；
    `/ota/progress` 同）
  - `GET /api/wifi/scan` `/api/wifi/status`（net_mgr 状态+热切换进度合并）
  - `POST /api/wifi/connect`（改走 net_provision_submit，web 配网通道）
  - `GET /api/status`（总览：uptime/rssi/free_heap/net 状态/ip）
  - `POST /api/web/update`（保留：tar 网页包免整机更新）
  - `GET /ws` WebSocket（保留）+ `web_ws_broadcast()` 公开广播接口
    （任意任务线程安全）；
- `web_server_start()` 改为内部自建任务、立即返回；`web_spiffs_init()`
  兼容 SPIFFS 已挂载场景（main 已挂时只做 hash 部署检查）。

**5. 接入与脚本**
- `main.c`：新增 `OVS_ENABLE_NET` 宏（默认 1），条件编译接入
  `ota_init → net_mgr_init → net_mgr_start(STA) → web_spiffs_init →
  web_server_start`；置 0 即纯净 VFS 测试环境；
- `scripts/ota_push.sh`（新）：等设备可达 → 单请求流式上传 → SHA256 比对 →
  等重启 → 打印新版本与回滚待确认状态。默认目标 `ovs.local`，固件
  `build/ovs.bin`；
- 删除 `scripts/ota_update.sh`（X-Offset 分块协议已废弃）；
  `sign_firmware.py` 保留（未来安全启动可复用）。

### 验证
- ✅ `idf.py build` 通过；本次新增/修改文件零警告（event_bus_test.c、
  tasker_test.c、cst816s.c 为既有告警，非本线引入）；
- ✅ 内置 SHA256 宿主机验证：""/"abc"/3MB 非对齐流式三组向量与 sha256sum
  完全一致；
- ✅ bootloader 配置确认 CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1；
- ✅ espressif/mdns 1.12.0 已入 dependencies.lock。

### 待硬件验证（需要串口时与 W25Q128 测试线协调 ttyACM0）
- [ ] 首次：串口 `idf.py flash` 一次（带新 bootloader，回滚生效的前提）
- [ ] 启动后确认：STA 连接（NVS 播种默认 SSID）→ mDNS `ovs.local` 可解析
- [ ] `curl http://ovs.local/api/ota/status` 返回正常
- [ ] `./scripts/ota_push.sh` 完整流程：上传→校验→重启→新版本确认
- [ ] 连续两次 OTA 验证 A/B 槽交替
- [ ] 回滚验证：确认 15s 内重启会回退旧固件（可选，构造坏固件测）
- [ ] AP 模式：net_mgr_start(NET_MODE_AP) 后 PC 连 OVS-xxxx 热点可访问
      Web 并推 OTA（开发期暂未默认开启，代码路径已就绪）

### 已知限制 / 下一步
- SHA256 对比目前是"提示"级别（esp_ota_end 才是权威校验）；若镜像尾部有
  后备数据可能导致比对不一致，以设备端 ok 响应为准；
- 蓝牙配网：实现 provider 后注册即可；LVGL 网络配置页可直接使用
  net_mgr_get_status + net_provision 接口；
- Web 前端（vue-ui）部署仍走固件内嵌 + hash 部署，或 /api/web/update tar；
- 未来安全切换：如需 HTTPS/签名 OTA，ota.c 已隔离传输与校验，可平滑升级。

### 代码变更清单（本线）
- 新增：components/modules/net_mgr/{net_mgr.c,net_mgr.h,net_types.h,
  net_provision.h,CMakeLists.txt}；components/modules/ota/ota_sha256.{c,h}；
  components/modules/web/web_ota.{c,h}；scripts/ota_push.sh；
  main/idf_component.yml；docs/development_log_net_ota.md
- 重写：components/modules/ota/{ota.c,ota.h}；components/modules/web/{web.c,web.h}
- 修改：components/drivers/wifi/{wifi.c,wifi.h}；
  components/core/event_bus/event_bus_types.h（+2 AP 事件）；
  main/main.c（条件编译块）；main/CMakeLists.txt；CMakeLists.txt；
  sdkconfig（回滚）；dependencies.lock
- 删除：scripts/ota_update.sh

## 2026-09-08 - OTA 使用文档 + main.c 保护区 + ota_push.sh WSL 适配

### 完成内容
- **main.c 保护区**：OTA 相关代码（OVS_ENABLE_NET 宏、三个 include、
  net_stack_init、app_main 调用点）加横幅注释，写明四条协作规则——
  宏必须保持 1、调用勿删、main.c 不写网络逻辑、改动先与 OTA 线确认。
  VFS 线的其他 main.c 修改与保护区互不冲突；
- **ota_push.sh 重写（WSL2 适配 + 结果反馈）**：
  - 主机解析三级回退：IP 直用 → getent（WSL 内）→ powershell.exe
    借 Windows 原生 mDNS 解析 ovs.local（WSL2 不走 mDNS 的解法）；
  - 全程反馈：上传进度条 + 耗时/速率 → 上传前记录旧版本 → 重启等待 →
    "✅ OTA 成功"结果块（旧版本(槽) → 新版本(槽)）→ pending_verify 时
    额外等 17s 验证回滚确认翻转并给最终结论；
  - 每步失败红色 ❌ + 原因 + 排查建议，非 0 退出码；
  - 已实测：语法检查通过，不可达主机的失败路径输出符合预期；
- **docs/ota_guide.md**：新增"main.c 保护区"章节与脚本反馈说明；
- **mybuild.sh**：移除已废弃的签名步骤，OTA 提示改为 ota_push.sh。

### 编译状态
- ✅ idf.py build 通过（main.c 注释改动后复验）

## 2026-09-08 - WiFi 配置入设备树 + 驱动/模块拆分 + 工程目录调整

### 完成内容
- **设备树新增 wifi 节点**（ovs.dtb.json，只增未动其他节点）：
  `wifi.sta.ssid/password`（出厂默认凭据）、`wifi.ap.{ssid_prefix,password,
  channel,max_connection}`、`wifi.default_mode`；
- **net_mgr 配置优先级**：显式 config > NVS（用户配网）> 设备树（播种 NVS）。
  wifi.h 中硬编码凭据删除，凭据配置入口收敛到设备树 + NVS；
- **WiFi 驱动/模块拆分**：
  - drivers/wifi 瘦身为纯驱动：协议栈/netif/启停/扫描/网络信息/断线自动重连，
    删除热切换回退、wifi_switch_ap、wifi_get_switch_status 等策略逻辑；
  - 热切换策略（30s 超时回退 RAM 配置 + NVS、切换状态跟踪）收编进 net_mgr，
    经 wifi_set_sta_config + wifi_start_sta 驱动原语实现；
    net_status_t 增加 switching/switch_elapsed_sec；
  - web.c 的 /api/wifi/status 改为纯 net_mgr 状态（含切换进度字段）；
- **工程目录调整**：
  - 应用代码 main.c/vfs_stress.c/h 移入 src/app/；main/ 保留为 CMake 垫片
    （IDF v6 强制要求名为 main 的组件且组件名取自目录名，垫片仅注册
    src/app 源文件）；main/idf_component.yml（mdns 依赖）移至 net_mgr/；
  - src/app/diy-smart-assistant 硬件资料移入 docs/diy-smart-assistant；
- **依赖修正**：net_mgr REQUIRES +dtbs（设备树）；ota REQUIRES 收敛为
  app_update/esp_partition/esp_app_format/esp_system/esp_timer/logger；
  web REQUIRES 更新（+net_mgr，-esp_http_server）。

### 编译状态
- ✅ idf.py build 通过，本线文件零警告
- 修复两处 -Werror=format-truncation（AP SSID 前缀拼接改定界拷贝）

### 待硬件验证（累计）
- [ ] 串口引导一次新固件（新 bootloader + 新设备树 SPIFFS）
- [ ] 确认日志：`[NET] STA credentials seeded from device tree: 150717`
- [ ] OTA 推送完整流程（scripts/ota_push.sh）

## 2026-09-08 - 修复：net_stack_init 早于 dtree_init 导致设备树 WiFi 配置不生效

### 现象（实机日志）
`[NET]: Network manager initialized (..., dtree=no)` + `STA start failed: no credentials`

### 根因
main.c 中 net_stack_init() 调用点原放在 tasker_init() 之后、
而 dtree_init() 在其后——net_mgr 读设备树时设备树尚未解析，
dtree_has_node("wifi") 恒为 false，凭据不播种、STA 无法启动。

### 修复
net_stack_init() 调用点移至 dtree_init() 成功之后、W25Q128 初始化之前，
保护区注释补充依赖顺序说明（依赖 NVS/event_bus/tasker/SPIFFS/dtree 就绪）。

### 注意
设备树 JSON 位于 SPIFFS 分区，OTA/app-flash 不会更新它；
变更 ovs.dtb.json 后必须完整 `idf.py flash`（自动含 SPIFFS 镜像）。

### 编译状态
- ✅ idf.py build 通过

## 2026-09-08 - 设备树 WiFi 配置变更覆盖 NVS（烧录即生效）

### 需求
烧录时以设备树 WiFi 配置为准，覆盖 NVS 旧值；用户配网凭据在设备树
未变时持续生效（跨 OTA 幸存）。

### 实现（net_mgr.c）
- NVS 新增 `dt_wifi_hash`（u32）：已应用设备树 WiFi 配置的 FNV-1a 哈希；
- 启动时计算当前设备树 wifi.sta 哈希与 NVS 已存哈希比对：
  - 不一致（= 烧录了新配置）→ 设备树凭据覆盖 NVS + 更新哈希；
  - 一致 → NVS 优先（用户配网凭据保留）；
  - NVS 意外为空但哈希一致 → 按设备树重播；
- 配网接口（net_provision_submit）不触碰该哈希 → 用户凭据不受影响。

### 生效语义（最终优先级）
显式 config > 设备树变更覆盖 > NVS（用户配网）> 无

### 编译状态
- ✅ idf.py build 通过
