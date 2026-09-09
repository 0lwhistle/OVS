# LVGL 9.5 双平台骨架 + PC 模拟器 设计文档

日期: 2026-09-09
状态: 已实施，真机 OTA 冒烟 PASS（flush_count 持续增长，无 panic；tick 时基补丁 lv_tick_set_cb）
关联: docs/REFACTORING_PLAN.md（六层 UI / 6.2-6.5 / R1 / C4）

## 1. 目标

- 引入 LVGL v9.5.0（上游源码 vendor），项目可分别构建出 **ESP32-S3 固件** 与 **PC 模拟器** 两个产物。
- PC 模拟器用 SDL2 窗口呈现与真机同一份 UI 代码，脱离硬件调 UI。
- 本期为**骨架**：显示走 null 输出冒烟，st7789/cst816s 真实对接留待下一步（方案 6.2/6.3）。

## 2. 架构：平台分叉收在边界，UI 层零条件编译

```
thirdparty/lvgl-9.5.0/      上游源码原样 vendor（裁掉 tests/demos/docs/examples 非运行时目录）
thirdparty/lvgl_lib/        ESP-IDF 包装组件（glob 上游 src/*.c + 注入 lv_conf 路径）
src/lvgl/                   产品 UI（两端编译同一份）
  ├── lv_conf.h             v9.5 双平台配置（唯一差异: OVS_SIMULATOR 时开 LV_USE_SDL）
  ├── lvgl_app.c/h          ESP 运行时：lv_init → display_port → nav → lvgl_task(Core1/16KB/prio4)
  ├── port/                 仅 ESP：display_port.c（null flush 冒烟）、bridge_time_esp.c（uptime）
  ├── navigator/nav.c       tileview 三横页 [intercom][home][clock]
  ├── pages/page_home.c     状态栏 + 48 号大字时钟 + 占位卡片（320×240 横屏）
  └── bridge/bridge_time.h  UI 唯一时间来源（ESP=uptime，PC=系统时间）
sim/                        PC 模拟器（独立 CMake 工程）
  ├── CMakeLists.txt        add_subdirectory(上游 lvgl, os_desktop.cmake 路线) + SDL2
  ├── main.c                lv_sdl_window(320×240, zoom 2x) + 鼠标 indev + 主循环
  └── bridge_time_sim.c     mock 桥（真实系统时间）
components/core/logger/     纯 printf 实现，PC 端直接复用，零改造
```

原则：
- UI 六层（pages/navigator/bridge 等）不出现任何平台 `#ifdef`；
- ESP 硬件差异全部收在 `src/lvgl/port/`，PC 差异全部收在 `sim/`；
- 模拟器不编译 `lvgl_app.c` 与 `port/`，用 `sim/main.c` 替代同一角色。

## 3. 关键实现决策

| 决策 | 内容 | 原因 |
|------|------|------|
| 组件命名 | 包装组件叫 `lvgl_lib`，UI 组件保持 `src/lvgl`（组件名 lvgl） | IDF 组件名=目录名，两个 `lvgl` 会重名冲突 |
| libs 不排除 | 上游 `src/*.c` 全量 glob，不做目录过滤 | v9.5 的 `src/libs/bin_decoder` 是 `lv_init` 必需解码器；未启用库由 `LV_USE_*` 宏守卫成空翻译单元（与上游 esp.cmake 行为一致）；thorvg 为 .cpp 被 *.c 通配自然排除 |
| lv_conf 注入 | IDF: `LV_CONF_INCLUDE_SIMPLE` + PUBLIC include `src/lvgl`；PC: 上游 CMake 的 `LV_BUILD_CONF_DIR` 变量 | 两端共用一份配置文件 |
| v8→v9 迁移 | 删除 `LV_COLOR_16_SWAP`/`LV_DRAW_COMPLEX`/`LV_MEM_CUSTOM` 等失效宏；`LV_USE_STDLIB_MALLOC=LV_STDLIB_CLIB`（避免内置 64KB 静态池占内部 RAM） | 旧 `lv_conf.h` 是 v8 风格，全量失效 |
| 字节交换 | v9 无 SWAP 宏，将来 st7789 flush_cb 内调 `lv_draw_sw_rgb565_swap()` | 因此一份 lv_conf 两端通用 |
| 刷新 | 30fps（`LV_DEF_REFR_PERIOD 33`） | SPI 屏 40MHz 局部刷新上限约 37fps |
| ESP 显示冒烟 | 320×240 display + 双 20 行内部 DMA 缓冲（2×12.8KB），flush_cb 仅计数，lvgl_task 每 5s 打 `flush_count` | OTA 后串口有明确存活证据；真屏点亮下一步 |
| 屏幕尺寸 | **320×240 横屏**（设备树 lcd_display 已同步修正，原 240×280 有误） | 用户确认实物面板像素尺寸 |
| OTA 隔离 | main.c 中 `lvgl_app_init()` 置于 `vfs_stack_start()` 之后，远离 OTA/网络保护区 | 遵守保护区协作规则 |

## 4. 构建与验收

- ESP: `idf.py build` → 固件 1.54MB，ota 分区余 41%（LVGL 增量约 +430KB）
- PC: `scripts/sim_build.sh [--run]` → `build-sim/ovs_sim`，窗口可鼠标滑动三页、时钟走字、关窗即退
- 真机: `./scripts/ota_push.sh <IP>` 后串口应见 `[LVGL] LVGL runtime started` 与周期 `smoke: flush_count=` 增长、无 panic（屏幕黑屏为本期预期）
- 本期实测：双端构建通过；模拟器运行验证通过；R1（LVGL v9 × IDF 6.0.1）编译链接层通过

## 5. 风险与后续

- R1 真机运行时行为待 OTA 冒烟确认（编译/链接已过）。
- st7789.c 仍硬编码 240×280，未读设备树——6.2 对接时一并修正（本期未动该驱动）。
- 中文 CJK 字体、cst816s indev、真实 flush、页面栈导航（nav_push/pop）均为下一步工作。
