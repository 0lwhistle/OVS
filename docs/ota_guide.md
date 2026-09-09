# OVS OTA 使用指南

> 适用：开发期固件迭代（改代码 → 编译 → WiFi 推送 → 设备自动重启运行新固件）
> 详细设计见 `docs/development_log_net_ota.md`

---

## 一、现在能用了吗？

**OTA 通道已在线运行**（2026-09-08 完成串口引导，此后日常迭代全程免串口），
且**固件与设备树都支持 OTA**：

| 内容 | 方式 | 生效 |
|------|------|------|
| 固件（app） | `ota_push.sh` 推 OVSO 容器 | 写备用槽 → 15s 回滚确认后切换 |
| 设备树（dtb） | 随 OVSO 容器一起推，或单独 `POST /api/dtb/firmware` | 写非活动 dtb 槽，与 app 配对试运行 |

> 历史备注：OTA 依赖设备上先运行一个含 Web 服务的固件，因此项目初期需要
> 一次串口引导烧录（含启用回滚保护的 bootloader）。该步骤早已完成，仅在
> 全新出厂设备/换 bootloader 配置/改分区表时才需要再做（见第四节）。

---

## 二、一次性引导（只做一次）

> 需要与隔壁 W25Q128 测试线协调串口（ttyACM0），确认对方没在占用时执行。

```bash
# 1. 确认网络开关已打开（当前默认就是 1，无需改动）
#    src/app/main.c:  #define OVS_ENABLE_NET  1

# 2. 编译
idf.py build

# 3. 串口烧录（含新 bootloader，回滚保护在此生效）
idf.py -p /dev/ttyACM0 flash

# 4. 看启动日志确认三件事：
idf.py -p /dev/ttyACM0 monitor
#    [NET]  STA connected, IP: 192.168.x.x   ← 记下这个 IP
#    [NET]  mDNS hostname: ovs.local
#    [WEB]  Web server started on port 80
```

看到这三行，OTA 通道就通了，之后可以 Ctrl+] 退出 monitor、拔掉串口。

---

## 三、日常迭代流程（免串口）

```bash
source scripts/env.sh       # 一次性环境初始化（脚本与 idf 命令免路径）
idf.py build                # 或 mybuild.sh（会多构建 Vue 前端）或 ovs_release
ota_push.sh                 # 默认推到 ovs.local（OVS_HOST 可改默认目标）
# 或一条龙：ovs_release --ota 192.168.2.111  （完整构建+推送）
```

设备树 OTA 详见第三节。

**脚本反馈（WSL2 适配）**：
- 主机解析自动三级回退：IP 直用 → WSL 内 `getent` → 借 `powershell.exe`
  用 Windows 原生 mDNS 解析 `ovs.local`（WSL2 本身不走 mDNS，脚本已处理）；
  都失败会明确提示改用 IP；
- 全程反馈：等待在线 → `curl` 上传进度条 + 耗时/速率 → 校验结果 →
  重启等待 → 最终结果块：
  `✅ OTA 成功` + 旧版本(槽) → 新版本(槽) 对比 + 回滚确认状态；
  开启回滚待确认时会额外等 17s 验证 pending_verify 翻转，给出最终结论；
- 任何一步失败：红色 ❌ + 具体原因 + 排查建议，退出码非 0。

也可以指定目标和固件：

```bash
./scripts/ota_push.sh 192.168.2.154          # 用 IP（最稳）
OVS_HOST=192.168.2.154 ./scripts/ota_push.sh # 环境变量方式
```

**回滚保护说明**：新固件启动后 15 秒内会"待确认"，稳定运行 15 秒后自动标记
有效。如果新固件启动即崩溃、或 15 秒内被断电，bootloader 下次启动自动回退
旧固件——**坏固件不会变砖**，但会表现为"推上去了又跑旧版本"，此时需要检查
代码为什么启动失败。

---

## 三点二、设备树 OTA（A/B 槽，与固件同级能力）

**改 `ovs.dtb.json` 只需 OTA，无需串口。** 机制（详见 `components/dtbs/dtb_ab`）：

- **容器与双槽**：build 时 `scripts/pack_dtb.py` 自动生成 `build/dtb.bin`
  （OVSO 容器 = 96B 头 + app + dtb 一次上传）；设备树写**非活动 dtb 槽**
  （dtb_0/dtb_1 各 64KB），NVS 里的事务性指针决定哪个槽生效。
- **配对试运行**：设备树与 app 捆绑走同一次 15s 回滚确认——新 app 崩溃回滚，
  **设备树一并回旧版**（配对防错配：旧固件 + 新树、新固件 + 旧树的组合不会
  持久存在）；15s 稳定后指针翻转，新树正式生效。
- **兜底**：双 dtb 槽全部损坏时，启动自动回退 SPIFFS 里的出厂设备树
  （`/spiffs/ovs.dtb.json`），系统不会因树损坏而变砖。
- **WiFi 凭据联动**：烧录含新 `wifi.sta` 配置的树后，下次启动按配置哈希检测
  变更**自动覆盖 NVS 旧凭据**；树未变时用户经 `/api/wifi/connect` 配的凭据
  持续生效且跨 OTA 幸存。

**单独只更新设备树**（不动固件）：

```bash
curl -X POST --data-binary @build/dtb.bin http://<设备IP>/api/dtb/firmware
# 重启后生效（走同样的 A/B 槽 + 指针切换）
```

**怎么确认树已切换**：

```bash
curl http://<设备IP>/api/ota/status     # 看 "dtb_slot" 字段（0/1）
# 串口启动日志：
#   [DTREE]: Loaded device tree from A/B slot N
```

**注意**：设备树内容变更走 OTA 即可；只有 `partitions.csv` 里 dtb 槽/littlefs
的位置变化这类**分区表改动**才需要串口（见第四节）。

---

## 三点五、main.c 中的 OTA 保护区（其他开发线必读）

`src/app/main.c` 中有一段带横幅注释的 **OTA/网络保护区**（main/ 只是 CMake 垫片，代码都在 src/app），包含：

| 元素 | 位置 | 作用 |
|------|------|------|
| `#define OVS_ENABLE_NET 1` | 文件头保护区 | 网络栈总开关，**必须保持 1** |
| `#include "net_mgr.h" "web.h" "ota.h"` | 保护区内部 | 模块头文件 |
| `static void net_stack_init()` | 保护区内部 | 初始化顺序：ota→net→web（内部顺序勿动） |
| `app_init_set_net_stack(net_stack_init)` | app_main（原调用点，带 ▶ 迁移注记） | 把函数指针注入 holder 注册表 |

> **2026-09-09 Phase 1 变更**：net_stack_init 的调用点已从 app_main 迁移至
> holder 编排（`src/app/app_init.c` 的 "net_stack" 模块：required、依赖 dtree
> 就绪后执行）。能力等价性经 OTA 双向验证，原调用点留有迁移注记。

协作规则（保护区横幅注释里也写了）：
1. `OVS_ENABLE_NET` 保持 1——置 0 的固件一旦推上设备，OTA 通道关闭只能串口救；
2. 不要删除 net_stack_init 的调用链（现为 app_main 注入 → holder 注册表执行）；
3. 不要在 main.c 里改网络逻辑，调行为请去 net_mgr/web/ota 各模块；
4. VFS 线增删 main.c 其他内容（测试函数等）不受影响，保护区在文件头部和
   app_main 各占一小段，正常并行修改不会冲突。
5. WiFi SSID/密码配置：设备树 `wifi.sta` 为准——烧录了含新 WiFi 配置的
   设备树后，下次启动**自动覆盖 NVS 旧配置**（按配置哈希检测变更）；
   设备树未变时，用户经 `/api/wifi/connect` 配置的凭据持续生效且跨 OTA 幸存。
   串口更新设备树需完整 `idf.py flash`（`ota`/`app-flash` 子命令不更新
   SPIFFS 里的设备树）；日常改设备树走 A/B 槽 OTA 即可（见第三节），无需串口。

---

## 四、什么情况仍然必须串口烧录

| 场景 | 原因 |
|------|------|
| 更换/修改 bootloader 配置（如 sdkconfig 的 BOOTLOADER_* 项） | bootloader 只能串口刷 |
| 修改 `partitions.csv` 分区表 | 分区表只能串口刷 |
| 新固件**启动即崩溃**触发了回滚 | 设备在跑旧固件，可再次 OTA 修好的版本，无需串口；但连 OTA 服务都起不来的 bug 需要串口救 |
| `OVS_ENABLE_NET` 置 0 的固件推上去后 | 网络栈没起，OTA 通道关闭，只能串口恢复 |

⚠️ 推给设备的固件必须保持 `OVS_ENABLE_NET 1`，否则下次只能串口。
建议隔壁 VFS 线改 main.c 时保留该宏与 `net_stack_init()` 调用。

注意：W25Q128 的分区布局变更（外部 flash）**不属于**上表，OTA 即可更新。
设备树 JSON 内容变更也**不属于**上表：走 A/B 槽 OTA 即可（仅 `partitions.csv`
里 dtb 槽/littlefs 位置变化这类分区表改动才需要串口）。

---

## 五、故障排查

**`ovs.local` 无法解析（WSL2 常见）**
WSL2 默认不走 mDNS。两个办法：
- 直接用 IP：`./scripts/ota_push.sh 192.168.x.x`（IP 在启动日志里，或路由器后台看）
- 在 Windows 侧（支持 mDNS）执行推送，或 WSL2 配置 avahi/systemd-resolved

**设备不可达**
```bash
ping <设备IP>                    # 先看网络通不通
curl http://<设备IP>/api/status  # 再看 web 服务在不在
```
WiFi 凭据不对/路由器变了：设备会无限重连，连不上就只能串口看日志或改 NVS。

**上传中途中断**
直接重跑 `ota_push.sh` 即可。设备端检测到连接断开会自动中止本次升级、
丢弃半截数据，随时可以重新推。

**SHA256 提示不匹配**
以设备端 `"status":"ok"` 为准（esp_ota_end 已做权威镜像校验）。SHA256 对比
只是额外提示项。

**想看设备当前状态**
```bash
curl http://ovs.local/api/ota/status
# {"state":"idle","received":0,...,"current_slot":0,"pending_verify":false,
#  "version":"0.1.0","project":"ovs","sha256":""}
```

---

## 六、HTTP API 速查（Web 前端可直接用）

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/ota/firmware` | 流式固件上传（纯 app 或 OVSO 容器：app+设备树） |
| GET  | `/api/ota/status` | 升级状态/进度/SHA256/槽位/版本（含 dtb_slot） |
| POST | `/api/dtb/firmware` | 独立设备树更新（body=dtb.bin，重启生效） |
| GET  | `/api/status` | 设备总览（uptime/rssi/free_heap/net） |
| GET  | `/api/wifi/scan` | 扫描附近 AP |
| GET  | `/api/wifi/status` | 网络状态 + 热切换进度 |
| POST | `/api/wifi/connect` | 提交 WiFi 凭据（`{"ssid":"x","password":"y"}`） |
| POST | `/api/net/mode` | 网络模式热切换（`{"mode":"sta"\|"ap"\|"off"}`，免重启） |
| POST | `/api/web/update` | tar 网页包更新（免整机 OTA） |
| GET  | `/api/hello` | 连通性测试 |
| GET  | `/ws` | WebSocket 状态推送 |

## 七、并行开发注意（两条线共用一台设备时）

- 两条线都可以各自 build 后 OTA；**同一时刻只有一个人推**，后推的会覆盖前推的
  （设备只有两个槽，每次 OTA 覆盖备用槽）。
- 设备上跑的永远是"最后一次推送"的固件——另一条线的改动如果还没推，
  重启后就没了；重要节点请各自 commit。
- OTA 写的是内部 flash（ota_0/ota_1），与 W25Q128 外部 flash 操作互不干扰，
  VFS 压力测试运行期间照样可以推送。
