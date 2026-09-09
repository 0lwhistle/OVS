# OVS项目开发交接摘要

## 最后更新时间
2026-09-09 (Asia/Shanghai)

## 当前项目状态
- **编译状态**：✅ 成功
- **版本控制**：✅ 已提交 GitHub（VFS 修复 + 设备树 A/B OTA + WiFi 热切换，2026-09-08）
- **硬件测试**：✅ VFS 挂载 4/4 + 压力测试 54/54、设备树 A/B OTA 实测闭环、
  WiFi AP/STA 热切换真机双向 PASS（详见 development_log.md 2026-09-08 各条目）

## 设备树（2026-09-08 重构后）
- **单棵树单文件**：`components/dtbs/config/ovs.dtb.json`（构建用此目录生成 SPIFFS 镜像）
- 设备节点嵌套在总线节点下（父子关系即挂载关系）；总线节点名即控制器编号
- 代码按 compatible 查询绑定：`dtree_find_by_compatible()` → `dtree_get_parent()` → 总线驱动
- 关键 API：`include/dtree.h`；已删除 `DTREE_HOST()`/按文件名挂根的加载方式
- **A/B 分区 OTA**：`components/dtbs/dtb_ab.{h,c}` 管理 dtb_0/dtb_1 裸分区
  （NVS 事务性指针 active/trial）；`ota_push.sh` 自动拼 OVSO 容器（app+dtb）
  一次上传，独立更新走 `POST /api/dtb/firmware`

## 近期完成工作（2026-09-08）
### 1. 设备树重构（嵌套单棵树 + compatible 绑定）
- 四个总线驱动 `*_drv_load_config()` 统一为 `(bus_node, config)` 签名 + compatible 校验
- st7789/w25q128/cst816s/aht30/lora/audio_module 全部改为 compatible 查询定位节点
- i2s_drv 与设备解耦（din/dout 由 audio_module 从 mic/amp 节点读取）
- 编译零警告；spiffs.bin 已含新设备树；详见 development_log.md 2026-09-08

### 2. 总线驱动共享计数（Linux 式，同日早前）
- SPI/I2C/I2S/UART 引用计数，总线只初始化一次（ST7789+W25Q128 共享 SPI2 不再冲突）
- W25Q128 健康状态机（READY/FAULT + JEDEC 探测 + 自动恢复）
- VFS 卸载重挂载修复（CONFIG_VFS_MAX_COUNT 8→16）

### 3. VFS 全链路修复（真机压测通过）
- 根因：设备 spiffs 分区设备树为旧版 + `CONFIG_LITTLEFS_MAX_PARTITIONS=3` 不够用
- 修复：串口重烧 app+spiffs；`CONFIG_LITTLEFS_MAX_PARTITIONS` 3→8
- 真机验证：4/4 挂载成功（/font /audio /media /config）、压测两轮 54/54 通过

### 4. 设备树 A/B 分区 OTA 落地（实测闭环）
- dtb_0/dtb_1 裸分区 + NVS 事务性指针（active/trial），随 app 15s 试运行配对回滚
- `ota_push.sh` 检测 build/dtb.bin 自动拼 OVSO 容器；独立通道 POST /api/dtb/firmware
- 双槽全废自动回退 /spiffs/ovs.dtb.json 出厂树

### 5. WiFi AP/STA 免重启热切换（真机双向 PASS）
- `net_mgr_switch_mode(mode, &prev)`：模式变化时发布 EVENT_WIFI_MODE_CHANGED
- Web 端点 `POST /api/net/mode`（`{"mode":"sta"|"ap"|"off"}`）
- main.c 自测开关 `OVS_NET_HOTSWAP_TEST`（默认 0）

## 待解决问题
1. W25Q128 每页写/扇区擦日志为 LOGD，INFO 级全打印会刷屏——监控时 grep 过滤
   或运行期 `logger_set_level()`
2. app_main 每次开机无条件跑 VFS+压力测试（~13 分钟），产品化前需加编译/运行开关
3. `wifi_scan_aps` AP 模式限制、OTA 后版本号差异化

## 下一步计划
1. 处理上述待解决问题
2. 之后开始下一模块开发（音频/LVGL UI 等）

## 关键文件路径
- **设备树**：`components/dtbs/config/ovs.dtb.json` + `include/dtree.h`
- **VFS 模块**：`components/core/ovs_vfs/`
- **开发日志**：`docs/development_log.md`（新条目追加在文件顶部）
- **架构文档**：`docs/ARCHITECTURE.md`

## 编译命令
```bash
source /home/olwhistle/dockerNow/esp32/ESP-IDF/esp-idf-v6.0.1/export.sh
idf.py build                          # 编译
idf.py -p /dev/ttyACM0 flash monitor  # 烧录+监控（含 SPIFFS）
```

## 硬件配置
- **开发板**：ESP32-S3（PSRAM OCT 40MHz 已启用，必须保持）
- **外部Flash**：W25Q128 (16MB)，与 LCD 共享 SPI2
- **串口**：/dev/ttyACM0

## 注意事项
1. **PSRAM 必须启用**：VFS/LittleFS 依赖
2. **改 ovs.dtb.json 后**：日常走 A/B 槽 OTA（`ota_push.sh` 自动拼 OVSO 容器，
   或 `POST /api/dtb/firmware`）；需串口更新时用完整 `idf.py flash`
   （自动带上 SPIFFS 分区的出厂回退树）
3. 设备树里加新设备：嵌套到对应总线节点下、写 compatible，模块用 find_by_compatible 定位

## 版本信息
- **ESP-IDF版本**：v6.0.1
- **LittleFS版本**：2.11.3
- **项目版本**：0.1.0
