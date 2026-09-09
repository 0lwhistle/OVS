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

（HUB 完成后填写：env.sh 初始化、mybuild.sh 烧录、monitor.sh 监控、
PC 门禁运行命令 tests/ovs_tests、固件/设备树/i18n 资源打包说明、
app_init 日志中各模块加载状态对照表）

## 2. [HUB] 数据中枢测试项

### 2.1 统一数据接口（i18n / sensor_cache / time_svc / sysinfo）
（待 HUB 填写）

### 2.2 AHT30 收口
（待 HUB 填写）

### 2.3 LoRa lora_tp（需两台设备，可延后）
（待 HUB 填写；注明 LEVEL 档位占位值与手册核实前置）

## 3. [GUI] 板端体验测试项

### 3.1 音频（audio_module 异步 + audio_player）
（待 GUI 填写：外放提示音/音量 5 档/静音/队列播放）

### 3.2 LVGL 界面（导航/主题/语言/主页数据卡/待机）
（待 GUI 填写：触摸导航、蓝白主题、语言热切换、时间/温湿度/网络卡、
standby 触发）

## 4. [WEB] Web 界面测试项

### 4.1 访问与入口
（待 WEB 填写：ovs.local / IP 访问、页面清单）

### 4.2 Dashboard / Network / Settings
（待 WEB 填写：温湿度/网络信息卡对照串口实际值、WiFi 扫描连接、
语言切换；附 curl 端点自测命令）

### 4.3 OTA 升级实测
（待 WEB 填写：前端校验拒绝样例→合法 .bin 上传→进度→重启→
/api/ota/status 与串口确认；回滚保护验证）

## 5. 已知限制与占位（三方 append-only）

- [HUB] LEVEL 档位数值为占位，待 DX-LR22 手册核实；time_svc NTP 未接
  （synced 恒 false）；lora_tp 真机项需两台设备与手册核实后才可执行；
- [GUI] 待机画面为占位接口（内容后续注入）；聊天页/LoRa 状态角标为
  占位（依赖 lora 集成阶段）；
- [WEB] （待 WEB 补充）
