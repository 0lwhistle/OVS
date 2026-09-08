# 开发日志

## 2026-09-03 - 项目启动

### 完成内容
- [x] 项目规划完成
- [x] main.c 框架搭建
- [x] 目录结构设计

### 当前状态
- main.c 引用了8个模块，但模块代码尚未实现
- 缺少的模块：wifi_manager, ble_manager, lora_manager, audio_manager, display_manager, sd_manager, web_server, ui_manager

### 待开发模块
1. sd_manager - SD卡存储（优先级高，其他模块依赖）
2. display_manager - 显示驱动（ST7789）
3. audio_manager - 音频处理（INMP441 + MAX98357A）
4. lora_manager - LoRa通讯（SX1278）
5. wifi_manager - WiFi管理
6. ble_manager - 蓝牙配网
7. web_server - Web上位机
8. ui_manager - LVGL界面

### 下一步计划
- 用户指定优先开发的模块

## 2026-09-03 - 硬件变更：W25Q128替代SD卡

### 变更内容
- 存储模块从 SD 卡改为 W25Q128 SPI Flash（128MB）
- W25Q128 引脚：VCC, GND, SLK(SCLK), DO(MISO), DI(MOSI)
- 原 sd_manager 模块改名为 flash_manager

### 影响范围
- 目录结构：sd_manager/ → flash_manager/
- 模块命名：sd_manager.h → flash_manager.h
- main.c 中的引用需要更新
- 项目文档需要更新

### 优势
- SPI 接口更简单（无需 SDIO/SPI 模式选择）
- W25Q128 驱动更轻量
- 成本更低，体积更小

## 2026-09-03 - 硬件模块确认

### 已确定模块清单
1. ESP32-S3-R16N8 核心板
2. ST7789 1.3寸显示屏（SPI）
3. CST816S 触控屏（I2C）
4. W25Q128 128MB Flash（SPI）
5. INMP441 麦克风（I2S）
6. MAX98357A 功放（I2S）
7. 扬声器 3W 8Ω
8. SX1278 LoRa 433MHz（SPI）
9. 18650+TP4056+升压电源模块

### 扬声器参数
- 阻抗：8Ω
- 功率：2-3W

## 2026-09-03 - 电源模块更新

### 电源方案
- 采用成品18650电源座模块（双节）
- 自带Type-C充电口、过充保护
- 背面输出：1.8V、3.3V、5V各5个排针
- 每个VCC配一个GND，共15个GND

### 供电分配
- 3.3V：ESP32-S3、显示屏、触控、W25Q128、麦克风、LoRa
- 5V：MAX98357A功放
- 1.8V：备用（暂未使用）

### 电池配置
- 12589mWh × 2 = 25178mWh
- 普通使用续航约38小时

## 2026-09-03 - 硬件资料更新：核心板引脚确认

### 变更内容
- ESP32-S3-R16N8 核心板排针为 22×2 = 44Pin（原记录为2×20=40Pin）
- 确认供电方式：电源模块5V → 核心板VIN引脚 → 板载LDO → 3.3V
- 获取完整44Pin引脚定义

### 引脚定义
**左边从上往下（22个引脚）**：
3V3, 3V3, GPIO4, GPIO5, GPIO6, GPIO7, GPIO15, GPIO16, GPIO17, GPIO18, GPIO8, GPIO3, GPIO46, GPIO9, GPIO10, GPIO11, GPIO12, GPIO13, GPIO14, VIN, GND

**右边从上往下（22个引脚）**：
GND, TX(GPIO43), RX(GPIO44), GPIO1, GPIO2, GPIO42, GPIO41, GPIO40, GPIO39, GPIO38, GPIO37, GPIO36, GPIO35, GPIO0, GPIO45, GPIO48, GPIO47, GPIO21, GPIO20, GPIO19, GND, GND

### 引脚分配确认
所有已分配的GPIO均在引脚图上，无冲突：
- SPI：GPIO10(MOSI), GPIO11(MISO), GPIO12(SCLK), GPIO13(CS_W25Q128), GPIO14(CS_LoRa), GPIO21(CS_LCD), GPIO47(DC_LCD), GPIO48(RST_LCD), GPIO4(BL)
- I2S：GPIO5(BCLK), GPIO6(WS), GPIO7(DIN), GPIO15(DOUT)
- I2C：GPIO16(SDA), GPIO17(SCL), GPIO18(INT)
- GPIO：GPIO0(BOOT), GPIO1(ADC), GPIO2(LED), GPIO3(LoRa_DIO0), GPIO8(LoRa_RST)

### 空闲可用引脚
GPIO19, GPIO20, GPIO35~42, GPIO45, GPIO46

### 文档更新
- 更新 `hardware/wiring_diagram.md`：排针数量改为22×2，添加完整引脚定义表，更新供电方式说明

## 2026-09-03 - 连接方式确认

### 连接方案
- ESP32-S3、LoRa、W25Q128、麦克风、功放：排母对接，可插拔
- FPC转接板：排针直接焊死在PCB上
- 电源模块：排母对接 + 扎带固定（PCB预留扎带孔）

### PCB排母/排针清单
- 22×2排母 × 1（核心板）
- 2×5排母 × 3（电源模块）
- 2×5排针 × 1（FPC转接板，焊死）
- 2×3排针 × 1（FPC转接板，焊死）
- 1×7排母 × 2（LoRa + 功放）
- 2×3排母 × 1（麦克风）
- 1×5排母 × 1（W25Q128）
- 扎带孔 × 若干（电源模块固定）

### 文档更新
- 更新 `hardware/wiring_diagram.md`：添加连接方式章节，更新排母清单

## 2026-09-03 - 触控屏引脚定义更新

### CST816S触控屏引脚定义（从原理图获取）
- Pin1: VDD (电源)
- Pin2: SCL (时钟)
- Pin3: SDA (数据)
- Pin4: GND (地)
- Pin5: INT (中断)
- Pin6: RST (复位)

### 接线对应
- Pin1 VDD → 3.3V
- Pin2 SCL → GPIO17 (I2C_SCL)
- Pin3 SDA → GPIO16 (I2C_SDA)
- Pin4 GND → GND
- Pin5 INT → GPIO18
- Pin6 RST → GPIO39 (触控RST)
- Pin6 RST → GPIO8（可选）

### 文档更新
- 更新 `hardware/wiring_diagram.md`：补充CST816S触控屏引脚定义

## 2026-09-03 - 显示屏模块整合更新

### 变更内容
- 将ST7789显示屏和CST816S触控屏合并为"电容触控显示屏"模块
- 显示屏模组引出两个FPC排线：
  - 显示FPC（10pin）：GND, RS(DC), CS, SCK, SDA, RST, VDD, GND, LEDA, LEDK
  - 触控FPC（6pin）：GND, AVDD, INT_A, I2C_SCL, I2C_SDA, RST
- FPC转接板作用：将两个FPC转成排针（5×2+3×2），方便接到PCB底板

### FPC转接板连接
- 5×2排针：转接显示FPC（10pin）
- 3×2排针：转接触控FPC（6pin）

### 模块编号更新
原7个模块（1.ST7789, 2.CST816S, 3.W25Q128, 4.麦克风, 5.功放, 6.LoRa, 7.电源）
现6个模块（1.电容触控显示屏, 2.W25Q128, 3.麦克风, 4.功放, 5.LoRa, 6.电源）

### 文档更新
- 更新 `hardware/wiring_diagram.md`：
  - 合并显示屏和触控模块为"电容触控显示屏"
  - 更新模块编号
  - 更新供电分配部分
  - 更新PCB排母/排针清单

## 2026-09-03 - LoRa模块更换为串口LR22

### 变更内容
- LoRa模块从SX1278（SPI）更换为LR22（串口，LLCC68方案）
- LR22是串口LoRa模块，通过UART通信，不需要SPI

### LR22引脚定义（从左到右）
- Pin1: GND
- Pin2: VCC (3.3V)
- Pin3: AUX（状态指示）
- Pin4: TX（串口发送）
- Pin5: RX（串口接收）
- Pin6: M1（模式选择）
- Pin7: M0（模式选择）

### ESP32-S3引脚分配
- GPIO19 (UART1_TX) → LR22 RX
- GPIO20 (UART1_RX) → LR22 TX
- GPIO35 (M0) → LR22 M0
- GPIO36 (M1) → LR22 M1
- GPIO37 (AUX) → LR22 AUX

### 工作模式（M1,M0）
- 00：高时效模式（默认）
- 01：AT模式
- 10：空中唤醒模式
- 11：休眠模式

### AUX引脚说明
- 高电平：数据发送中/接收中/模式切换中
- 低电平：数据发送完成/接收完成/模式切换完成

### UART分配
- UART0：调试串口（GPIO43/44）
- UART1：LR22 LoRa（GPIO19/20）
- UART2：预留

### 同步更新W25Q128和MAX98357A引脚顺序
- W25Q128（从左到右）：VCC, CS, DO, GND, SLK, DI
- MAX98357A（从左到右）：VCC, GND, SD, GAIN, DIN, BCLK, LRC

### 文档更新
- 更新 `hardware/wiring_diagram.md`：
  - LoRa模块从SX1278改为LR22（串口）
  - 添加UART接口部分
  - 更新W25Q128和MAX98357A引脚顺序
  - 更新模块连接方案和PCB排母清单

## 2026-09-03 - 电源需求分析

### 屏幕电源参数（来自官方手册）
- VCI（模拟供电）：2.5-3.3V，典型2.8V
- VDD（数字供电）：2.5-3.3V，典型2.8V
- IOVCC（接口电压）：1.65-3.3V，典型2.8V
- 结论：全部可以用3.3V供电

### 电源模块输出能力
- 2000mAh电池：5V/1A输出
- 3000mAh电池：5V/1.5A输出
- MAX输出电流：1.5A
- 公式：I(in) = (5*I(out))/(0.7*V(in))

### 3.3V电流需求（电源模块3.3V直接供电）
- 屏幕（VDD+IOVCC+触控）：~60mA
- W25Q128 Flash：~25mA
- INMP441 麦克风：~1.4mA
- LR22 LoRa（发送时）：~120mA
- **合计：~206mA**

### 5V电流需求（电源模块5V供电）
- ESP32-S3（5VIN）：~240mA
- MAX98357A 功放（满功率）：~500mA
- **合计：~740mA**

### 电源余量分析
- 2000mAh电池：740mA需求，1A输出，余量260mA，勉强够用
- 3000mAh电池：740mA需求，1.5A输出，余量760mA，✅ 推荐

### 建议
- 选择3000mAh电池，余量充足，续航更长（约4小时连续使用）

### 文档更新
- 更新 `hardware/wiring_diagram.md`：添加电源需求分析章节

## 2026-09-03 - 添加ATH30温湿度传感器

### 新增模块
- 型号：ATH30 温湿度传感器
- 接口：I2C（与触控屏共享GPIO16/17）
- 供电电压：2.0-5.5V（使用3.3V）

### ATH30引脚定义（从左到右）
- Pin1: SDA → GPIO16（I2C数据，与触控屏共享）
- Pin2: SCL → GPIO17（I2C时钟，与触控屏共享）
- Pin3: GND → GND
- Pin4: VIN → 3.3V

### 模块数量更新
原6个模块 + ATH30 = 7个模块：
1. 电容触控显示屏
2. W25Q128 Flash
3. INMP441 麦克风
4. MAX98357A 功放
5. LR22 LoRa（串口）
6. ATH30 温湿度传感器（I2C）
7. 电源模块

### PCB排母更新
新增：1×4排母 × 1（ATH30温湿度传感器）

### 3.3V电流需求更新
- 新增ATH30：~2mA
- 新合计：~208mA（原206mA + 2mA）

### 文档更新
- 更新 `hardware/wiring_diagram.md`：
  - 添加ATH30温湿度传感器模块（第6个）
  - 更新I2C接口说明（触控 + ATH30）
  - 更新供电分配部分
  - 更新模块连接方案表格
  - 更新PCB排母/排针清单
  - 更新3.3V电流需求

## 2026-09-03 - 核心板引脚定义修正

### 修正内容
- 左边引脚Pin3原记录为GPIO4，实际为RST（复位引脚）
- 左边引脚从Pin4开始才是GPIO4

### 左边引脚定义（从上往下）
- Pin1: 3V3
- Pin2: 3V3
- Pin3: RST（复位引脚，低电平有效）
- Pin4-Pin22: GPIO4, GPIO5, GPIO6, GPIO7, GPIO15, GPIO16, GPIO17, GPIO18, GPIO8, GPIO3, GPIO46, GPIO9, GPIO10, GPIO11, GPIO12, GPIO13, GPIO14, VIN, GND

### 文档更新
- 更新 `hardware/wiring_diagram.md`：修正左边引脚定义，添加RST引脚

## 2026-09-03 - ESP32-S3引脚分配完成

### 分配原则
1. 不可冲突，每个引脚要查阅官方资料，不可与官方用途冲突
2. 引脚充足，能不复用就不复用
3. 引脚不足时，找不会同时使用的功能复用
4. 禁用USB/JTAG功能，释放相关引脚

### 引脚分配方案

#### SPI总线（显示屏 + W25Q128共享）
- GPIO10: MOSI
- GPIO11: MISO
- GPIO12: SCLK
- GPIO13: CS_W25Q128
- GPIO21: CS_LCD
- GPIO47: DC_LCD
- GPIO48: RST_LCD
- GPIO4: BL_LCD

#### I2S总线（麦克风 + 功放共享）
- GPIO5: BCLK
- GPIO6: WS
- GPIO7: DIN（麦克风）
- GPIO15: DOUT（功放）

#### I2C总线（触控 + ATH30共享）
- GPIO16: SDA
- GPIO17: SCL
- GPIO8: INT（触控中断，原GPIO18改为GPIO8）

#### UART1（LR22 LoRa）
- GPIO19: TX
- GPIO20: RX

#### LoRa控制
- GPIO35: M0
- GPIO36: M1
- GPIO37: AUX

#### 其他功能
- GPIO1: 电池ADC
- GPIO2: 状态LED
- GPIO9: W25Q128检测
- GPIO0: BOOT
- GPIO43: UART0 TX
- GPIO44: UART0 RX

### 空闲引脚
GPIO3, 14, 18, 38, 39, 40, 41, 42, 45, 46（共10个）

### 禁用USB/JTAG说明

#### 需要禁用的功能
- USB Serial/JTAG
- USB OTG

#### 受影响的引脚（共8个）
- GPIO4: JTAG MTDI → 显示屏背光
- GPIO5: JTAG MTCK → I2S BCLK
- GPIO6: JTAG MTDO → I2S WS
- GPIO15: USB JTAG D+ → I2S DOUT
- GPIO19: USB OTG D- → UART1 TX
- GPIO20: USB OTG D+ → UART1 RX
- GPIO47: USB Serial D+ → 显示屏DC
- GPIO48: USB Serial D- → 显示屏RST

#### 软件配置（必须在代码中配置）
```c
// 禁用USB Serial JTAG
esp_efuse_disable_rom_download_mode();
```

或在menuconfig中：
```
Component config → USB Serial JTAG → 禁用
Component config → USB OTG → 禁用
```

#### 影响
- ✅ COM接口（UART0）正常工作
- ❌ USB接口无法使用
- ✅ 串口烧录和监控不受影响

### 文档更新
- 更新 `hardware/wiring_diagram.md`：
  - 添加ESP32-S3引脚分配总表
  - 添加禁用USB/JTAG说明
  - 添加软件配置方法

## 2026-09-03 - 创建硬件注意事项文档

### 文档创建
创建了两个重要文档，确保软件开发时不会忽略硬件限制：

#### 1. `docs/hardware_notes_for_software.md`
- 硬件角度的软件开发注意事项
- 包含总线共用说明、禁用USB/JTAG配置
- 包含引脚分配总表、电源注意事项
- 包含模块通信协议、开发流程检查清单
- 包含常见问题排查方法

#### 2. `docs/skill_hardware_requirements.md`
- Skill硬件注意事项补充
- 软件开发必读流程
- 代码编写规范
- 测试验证清单
- 常见问题排查

### 重要提醒
**每次开发软件前必须先阅读 `docs/hardware_notes_for_software.md`！**

### 为什么必须阅读
1. **总线共用**：SPI、I2S、I2C总线都是多个设备共用的，操作不当会导致设备冲突
2. **禁用USB/JTAG**：必须禁用USB/JTAG功能，否则会影响引脚功能
3. **电源管理**：需要正确处理电池电压检测和低电量保护
4. **通信协议**：每个模块的通信协议和参数都不同

### 软件开发前检查清单
- [ ] 阅读 `docs/hardware_notes_for_software.md`
- [ ] 确认禁用USB/JTAG配置
- [ ] 确认I2C设备地址（触控0x15，ATH30 0x38）
- [ ] 确认SPI设备片选引脚（显示屏GPIO21，W25Q128 GPIO13）

### 文档更新
- 创建 `docs/hardware_notes_for_software.md` - 硬件详细注意事项
- 创建 `docs/skill_hardware_requirements.md` - Skill硬件注意事项补充

## 2026-09-03 - 添加AI行为规则

### 规则内容
- **长篇内容输出规则**：长篇内容（超过500字）直接输出成md文档，不要在终端显示
- **原因**：节省token，提高效率
- **执行方式**：创建md文档到 `docs/` 目录，告诉用户文档路径

### 文档创建
- 创建 `docs/ai_behavior_rules.md` - AI行为规则文档
- 记录长篇内容输出规则
- 记录文档命名规范
- 记录文档存放位置

### 影响
- 以后长篇内容会自动保存为md文档
- 用户需要查看文档了解详细内容
- 终端只显示简短的提示信息

### 文档更新
- 创建 `docs/ai_behavior_rules.md` - AI行为规则文档

## 2026-09-03 - 麦克风L/R引脚配置说明

### 配置说明
- INMP441麦克风L/R引脚接GND
- 表示使用**左声道**输出

### L/R引脚配置选项
- 接GND：左声道输出（当前配置）
- 接VDD：右声道输出
- 悬空：不确定，不推荐

### 软件处理
```c
// I2S立体声数据：buffer[0]=左声道, buffer[1]=右声道
int16_t i2s_buffer[2];
int16_t mic_data = i2s_buffer[0];  // 只使用左声道
```

### 文档更新
- 更新 `hardware/wiring_diagram.md`：添加INMP441麦克风L/R引脚详细说明
- 更新 `docs/hardware_notes_for_software.md`：添加L/R引脚配置和软件处理方法

## 2026-09-03 - 修正LR22 LoRa模块引脚顺序

### 错误发现
- 原记录的引脚顺序错误
- 正确的引脚顺序：GND, VCC, AUX, TX, RX, M1, M0

### 修正内容

#### 正确的引脚定义（从左到右）
- Pin1: GND - 电源地
- Pin2: VCC - 电源正（3.3V）
- Pin3: AUX - 状态指示（输入）
- Pin4: TX - 串口发送
- Pin5: RX - 串口接收
- Pin6: M1 - 模式选择（高电平=1）
- Pin7: M0 - 模式选择（高电平=1）

#### ESP32-S3引脚对应
- Pin1 GND → GND
- Pin2 VCC → 3.3V
- Pin3 AUX → GPIO37
- Pin4 TX → GPIO20（UART1 RX）
- Pin5 RX → GPIO19（UART1 TX）
- Pin6 M1 → GPIO36
- Pin7 M0 → GPIO35

### 文档更新
- 更新 `hardware/wiring_diagram.md`：修正LR22 LoRa模块引脚顺序
- 更新 `docs/hardware_notes_for_software.md`：添加正确的引脚定义

## 2026-09-03 - Skill启动会话

### 当前状态
- 已读取开发日志，了解项目进度
- main.c框架已搭建，引用8个模块
- 所有模块代码尚未实现
- 硬件文档完善，引脚分配已确认

### 待开发模块
1. flash_manager - W25Q128 Flash存储
2. display_manager - ST7789显示驱动
3. audio_manager - INMP441麦克风 + MAX98357A功放
4. lora_manager - LR22 LoRa通讯
5. wifi_manager - WiFi管理
6. ble_manager - 蓝牙配网
7. web_server - Web上位机
8. ui_manager - LVGL界面

### 下一步
- 等待用户指定优先开发的模块

## 2026-09-04 - PCB被动器件设计指南

### 完成内容
- [x] 创建PCB被动器件设计指南文档
- [x] 统一使用0805封装（手工焊接友好）
- [x] 添加复位按钮电路
- [x] 确认电池电压检测和LED指示灯功能
- [x] 验证引脚分配无冲突

### 文档创建
- 创建 `docs/pcb_passive_components_guide.md`
- 包含各模块去耦电容配置
- 包含完整BOM采购清单
- 包含PCB布局建议

### 关键器件清单

#### 电容（全部0805封装）
- 100nF × 12个（去耦）
- 10µF × 8个（滤波）
- 100µF × 2个（功放专用，1206/钽）

#### 电阻（全部0805封装）
- 10kΩ × 6个（上拉/限流）
- 4.7kΩ × 2个（I2C上拉）
- 100kΩ × 2个（电池分压）
- 220Ω × 1个（LED限流）
- 0Ω × 1个（功放增益）

#### 其他
- 轻触按键 6×6×5mm × 1个（复位）
- 0805 LED红色 × 1个（状态指示）

### 确认的功能
1. **电池电压检测** - GPIO1(ADC)，100kΩ+100kΩ分压
2. **LED状态指示** - GPIO2，220Ω限流
3. **复位按钮** - RST(Pin3)，10kΩ上拉

### 引脚分配确认
- GPIO1: ADC - 电池电压检测 ✅
- GPIO2: LED - 状态指示灯 ✅
- 无引脚冲突

### 下一步计划
- 等待用户PCB设计完成
- 开始软件模块开发

## 2026-09-04 - 移除电池电压检测功能

### 变更内容
- 移除电池电压检测电路
- 原因：电池模块没有直接引出电池正极，只有3.3V/5V供电引脚
- GPIO1释放，变为空闲引脚

### 影响范围
- 移除器件：R7(100kΩ), R8(100kΩ), C17(100nF)
- GPIO1：电池电压检测 → **空闲引脚**
- 采购清单更新：减少3个器件

### 引脚分配变更
| GPIO | 原功能 | 新状态 |
|------|--------|--------|
| GPIO1 | 电池电压检测 | **空闲** |

### 保留功能
- LED状态指示 (GPIO2)
- 复位按钮 (RST)

### 文档更新
- 更新 `docs/pcb_passive_components_guide.md`
- 更新采购清单

---

## 🚩 开发标志位

### 当前阶段
**阶段：** 硬件设计阶段（PCB设计中）
**状态：** ❌ 不写代码

### 规则
1. **不修改用户代码框架** - 用户已写好main.c框架，等用户提供后再开发
2. **不创建模块代码** - 等用户指定优先级后再写
3. **只更新硬件文档** - 确保软件开发时不出现硬件兼容问题

### 已完成的硬件文档
- [x] `hardware/wiring_diagram.md` - 引脚分配
- [x] `docs/hardware_notes_for_software.md` - 软件开发注意事项
- [x] `docs/pcb_passive_components_guide.md` - PCB被动器件指南
- [x] `docs/skill_hardware_requirements.md` - Skill硬件要求

### 待办（等用户指示）
- [ ] 用户提供代码框架
- [ ] 用户指定优先开发的模块
- [ ] 开始模块代码开发

---

**标志位说明：** 看到此标志位，表示当前只做硬件文档，不写代码。等用户说"开始写代码"后再进入软件开发阶段。

## 2026-09-04 - 添加ATH30温湿度传感器电路

### 完成内容
- [x] 在PCB被动器件指南中添加ATH30电路
- [x] 更新采购清单（电容数量+2）

### 电路设计
- VCC去耦：C19(100nF) + C20(10µF)
- I2C上拉：复用R5、R6（已在I2C总线章节）
- 设备地址：ATH30=0x38，触控=0x15

### 采购清单更新
- 100nF：12个 → 13个
- 10µF：8个 → 9个

### 文档更新
- 更新 `docs/pcb_passive_components_guide.md`

## 2026-09-04 - LED引脚变更：GPIO2→GPIO14

### 变更内容
- LED状态指示灯从GPIO2改为GPIO14
- 原因：用户调整引脚分配

### 引脚变更
| GPIO | 原功能 | 新功能 |
|------|--------|--------|
| GPIO2 | LED | 空闲 |
| GPIO14 | 空闲 | LED |

### 更新的文档
- [x] `hardware/wiring_diagram.md` - 引脚分配表
- [x] `docs/pcb_passive_components_guide.md` - LED电路和引脚表

### 空闲引脚更新
- 原：GPIO1, GPIO3, GPIO14, GPIO18, GPIO38~42, GPIO45, GPIO46
- 新：GPIO1, GPIO2, GPIO3, GPIO18, GPIO38~42, GPIO45, GPIO46

## 2026-09-04 - 引脚迁移：左边→右边排母

### 变更内容
将多个功能引脚从左边排母迁移到右边排母，方便布线。

### 迁移列表

| 原引脚 | 功能 | 新引脚 | 右边位置 |
|--------|------|--------|----------|
| GPIO4 | 显示屏背光 | GPIO38 | Pin10 |
| GPIO8 | 触控复位 | GPIO39 | Pin9 |
| GPIO10 | SPI MOSI | GPIO40 | Pin8 |
| GPIO11 | SPI MISO | GPIO41 | Pin7 |
| GPIO12 | SPI SCLK | GPIO42 | Pin6 |

### 已在右边的引脚（无需迁移）
- GPIO16: I2C SDA
- GPIO17: I2C SCL
- GPIO18: 触控INT

### 迁移后效果
- **右边排母**：SPI + I2C + 触控 + 背光（显示屏相关全部在右边）
- **左边排母**：I2S（麦克风+功放）
- 布线更集中，PCB更整洁

### 更新的文档
- [x] `hardware/wiring_diagram.md` - 引脚分配表
- [x] `docs/pcb_passive_components_guide.md` - 引脚分配表
- [x] `logs/development_log.md` - 本日志

### 空闲引脚更新
原：GPIO1, GPIO2, GPIO3, GPIO38~42, GPIO45, GPIO46
新：GPIO1, GPIO2, GPIO3, GPIO4, GPIO8, GPIO9, GPIO10, GPIO11, GPIO12, GPIO18, GPIO45, GPIO46

## 2026-09-04 - 更新44Pin引脚定义表

### 变更内容
在 `hardware/wiring_diagram.md` 的44Pin引脚定义表中添加了功能说明列。

### 更新后的格式
| Pin | GPIO | 功能 | 说明 |
|-----|------|------|------|
| 1 | 3V3 | 电源 | 3.3V输出 |
| ... | ... | ... | ... |

### 新增信息
- 每个引脚的功能标注（如I2S_BCLK、SPI_SCLK等）
- 每个引脚的说明（如时钟、数据、片选等）
- 空闲引脚明确标注"未使用"

### 文档更新
- [x] `hardware/wiring_diagram.md` - 44Pin引脚定义表

## 2026-09-04 - 全面更新wiring_diagram.md

### 变更内容
全面检查并更新 `hardware/wiring_diagram.md`，确保所有引脚分配一致。

### 主要更新
1. **44Pin引脚定义表** - 添加功能说明列
2. **SPI接口** - 更新为新引脚（GPIO40/41/42）
3. **触控接口** - 更新RST引脚为GPIO39
4. **显示屏背光** - 更新为GPIO38
5. **引脚分配总表** - 完整更新左右排母
6. **模块接线详解** - 更新所有模块接线
7. **空闲引脚列表** - 更新为最新状态

### 引脚迁移确认
| 原引脚 | 新引脚 | 功能 |
|--------|--------|------|
| GPIO10 | GPIO40 | SPI MOSI |
| GPIO11 | GPIO41 | SPI MISO |
| GPIO12 | GPIO42 | SPI SCLK |
| GPIO4 | GPIO38 | 显示屏背光 |
| GPIO8 | GPIO39 | 触控复位 |

### 文档备份
- 原文档备份：`hardware/wiring_diagram.md.bak`
- 新文档：`hardware/wiring_diagram.md`

### 验证结果
- ✅ 44Pin引脚定义表完整
- ✅ 所有接口分配一致
- ✅ 模块接线详解完整
- ✅ 空闲引脚列表正确

## 2026-09-05 - 触控屏引脚顺序更正

### 变更内容
- 根据实际到货的SPI电容触控屏，更正CST816S触控屏引脚顺序
- 实际引脚顺序（从1到6）：VDD, SCL, SDA, GND, INT, RST

### 引脚定义更新
- Pin1: VDD (电源)
- Pin2: SCL (时钟)
- Pin3: SDA (数据)
- Pin4: GND (地)
- Pin5: INT (中断)
- Pin6: RST (复位)

### 接线对应更新
- Pin1 VDD → 3.3V
- Pin2 SCL → GPIO17 (I2C_SCL)
- Pin3 SDA → GPIO16 (I2C_SDA)
- Pin4 GND → GND
- Pin5 INT → GPIO18
- Pin6 RST → GPIO39 (触控RST)

### 更新的文档
- [x] `logs/development_log.md` - 更新触控屏引脚定义和接线对应
- [x] `hardware/wiring_diagram.md` - 更新模块接线详解中的触控接口表格
- [x] `docs/hardware_notes_for_software.md` - 更新触控中断引脚为GPIO18

### 验证结果
- ✅ 引脚顺序与实际硬件一致
- ✅ 接线对应关系正确
- ✅ 中断引脚更新为GPIO18
- ✅ 空闲引脚列表正确（GPIO8成为空闲）

## 2026-09-05 - 屏幕尺寸更正

### 变更内容
- 屏幕实际尺寸从1.3寸更正为2.8寸
- 模块物理尺寸：约68mm×49mm
- 分辨率更新为320×240像素

### 更新的文档
- [x] `project_plan.md` - 更新屏幕尺寸和分辨率
- [x] `docs/hardware_notes_for_software.md` - 更新分辨率
- [x] `hardware/wiring_diagram.md` - 添加屏幕物理尺寸信息

### 影响
- PCB底板尺寸需要重新规划
- 外壳设计需要考虑屏幕尺寸
- LVGL界面布局可能需要调整

### 底板尺寸建议
- 屏幕模块：68mm×49mm
- ESP32-S3核心板：约55mm×20mm
- 其他模块：W25Q128、麦克风、功放、LoRa、ATH30、电源模块
- 建议底板尺寸：≥120mm×80mm（考虑布局和走线）

## 2026-09-05 - 底板布局设计

### 设计要求
- 屏幕居中放置
- 电源模块放置背面（88mm×44mm）
- 其他模块尺寸：≤25mm×20mm
- 外壳需要适配底板尺寸

### 模块尺寸参考
- 屏幕模块：68mm×49mm（2.8寸）
- 电源模块：88mm×44mm（双节18650）
- ESP32核心板：约55mm×20mm（22×2排母）
- 其他模块：W25Q128、麦克风、功放、LoRa、ATH30（均≤25mm×20mm）

### 布局方案
#### 正面布局
```
┌─────────────────────────────────────┐
│         屏幕区域 (68×49mm)           │
│    ┌─────────────────────────┐      │
│    │                         │      │
│    │      2.8寸触控屏         │      │
│    │                         │      │
│    └─────────────────────────┘      │
├─────────────────────────────────────┤
│ ESP32核心板 │ W25Q128 │ 麦克风     │
│ (55×20mm)  │         │            │
├─────────────────────────────────────┤
│    LoRa模块    │   ATH30   │ 功放   │
│               │           │        │
└─────────────────────────────────────┘
```

#### 背面布局
```
┌─────────────────────────────────────┐
│         电源模块 (88×44mm)           │
│    ┌─────────────────────────┐      │
│    │                         │      │
│    │  18650+TP4056+升压      │      │
│    │                         │      │
│    └─────────────────────────┘      │
└─────────────────────────────────────┘
```

### 底板尺寸计算
#### 宽度方向
- 电源模块宽度：88mm
- 屏幕宽度：68mm
- 建议底板宽度：90mm（容纳电源模块，屏幕居中后两侧各11mm空间）

#### 长度方向
- 电源模块高度：44mm
- 屏幕高度：49mm
- 其他模块需要空间
- 建议底板长度：120mm（屏幕49mm + 其他模块空间 + 边距）

### 推荐底板尺寸
- **底板尺寸：90mm × 120mm**
- 屏幕居中，左右各11mm边距
- 电源模块背面居中放置
- 其他模块围绕屏幕布局

### 外壳设计考虑
- 屏幕开口：比屏幕显示区域大2-3mm（约71mm×52mm）
- 外壳厚度：建议5-10mm
- 底板安装孔：四角或边缘
- 电源接口：Type-C开口在侧面或背面

## 2026-09-05 - 底板尺寸更正

### 变更内容
- 电源模块尺寸更正：85mm×44mm（原88mm×44mm）
- 当前底板尺寸：86mm×70mm

### 布局挑战
- 底板宽度86mm，电源模块宽度85mm → 电源模块几乎占满宽度
- 屏幕68mm×49mm，居中后左右各9mm空间
- ESP32核心板55mm×20mm，需要合理布局

### 新布局方案
#### 正面布局（屏幕居中）
```
┌────────────────────────────────────┐
│          屏幕区域 (68×49mm)         │
│     ┌────────────────────────┐     │
│     │                        │     │
│     │      2.8寸触控屏        │     │
│     │                        │     │
│     └────────────────────────┘     │
├────────────────────────────────────┤
│ ESP32核心板(55×20mm) │ W25Q128    │
├────────────────────────────────────┤
│  LoRa  │  麦克风  │  功放  │ ATH30 │
└────────────────────────────────────┘
```

#### 背面布局
```
┌────────────────────────────────────┐
│        电源模块 (85×44mm)           │
│     ┌────────────────────────┐     │
│     │                        │     │
│     │   18650+TP4056+升压    │     │
│     │                        │     │
│     └────────────────────────┘     │
└────────────────────────────────────┘
```

### 尺寸验证
- 底板：86mm×70mm
- 屏幕：68mm×49mm → 居中后左右各9mm，上下各10.5mm
- 电源模块：85mm×44mm → 背面居中，左右各0.5mm，上下各13mm
- ESP32核心板：55mm×20mm → 可放在屏幕下方（高度20mm，可用空间21mm）

### 外壳设计
- 屏幕开口：71mm×52mm（比显示区域大3mm）
- 外壳厚度：5mm
- 安装孔：四角，M3螺丝
- 电源接口：Type-C开口在侧面

## 2026-09-05 - 用户确认布局方案

### 确认内容
- 底板尺寸：86mm×70mm
- 屏幕居中放置（68mm×49mm）
- 电源模块背面放置（85mm×44mm）
- 其他模块围绕屏幕布局

### 当前状态
- 硬件设计阶段（PCB设计中）
- 标志位：❌ 不写代码（等待用户提供代码框架和指定开发优先级）

### 下一步计划
- 用户继续完成PCB底板设计
- 等待用户指定软件开发优先级
- 等待用户提供代码框架（如需要）

## [2026-09-06] - 开始软件开发

### 当前状态
- 硬件设计阶段（PCB设计中）
- 代码框架已建立（main.c, pin_config.h, app_init.h）
- 缺少所有模块实现（lora_manager, audio_manager, display_manager等）
- 用户输入了技能名称，可能想开始软件开发

### 下一步
- 询问用户具体开发需求
- 确定开发优先级
- 开始实现具体模块


## [2026-09-06] - PCB设计规则分析

### 完成内容
- [x] 分析了 Config_DesignRule_PCBv2.0_2026-09-06.json 设计规则
- [x] 从软件角度评估了规则的合理性
- [x] 提供了针对ESP32-S3智能助手的布线建议

### 规则分析结果
1. **安全间距规则**:
   - Track-Track: 4.0157mil (0.102mm) - 偏小，建议≥6mil
   - Track-SMD Pad: 5.9843mil (0.152mm) - 合理
   - Track-TH Pad: 5.9843mil (0.152mm) - 合理
   - Track-Via: 5.9843mil (0.152mm) - 合理
   - Track-Board Outline: 11.8mil (0.3mm) - 合理

2. **过孔尺寸规则**:
   - 外径默认: 24.0158mil (0.61mm) - 合理
   - 内径默认: 12.0078mil (0.305mm) - 合理

3. **差分对规则**:
   - 差分对间距: 6mil (0.152mm) - 合理
   - 长度容差: 10mil (0.254mm) - 合理

### 软件角度建议
1. **高速信号布线**:
   - SPI总线 (GPIO40,41,42): 建议线宽≥10mil，间距≥8mil
   - I2S总线 (GPIO5,6,7,15): 建议线宽≥8mil，间距≥6mil
   - 信号线长度应<50mm，避免直角走线

2. **电源完整性**:
   - 3.3V电源线: 建议宽度≥20mil
   - 地线: 建议宽度≥20mil
   - 去耦电容应靠近ESP32电源引脚

3. **信号完整性**:
   - I2C总线 (GPIO16,17): 需要4.7kΩ上拉电阻
   - 中断信号 (GPIO18): 需要上拉电阻
   - SPI时钟信号: 建议端接电阻

4. **EMC考虑**:
   - ESP32天线区域避免铺铜
   - 高速信号线需要包地处理
   - 电源入口需要EMI滤波器

### 待解决问题
- 需要确认走线宽度规则的具体数值
- 需要确认阻抗控制要求

### 下一步
- 用户继续PCB布线
- 根据软件需求优化布线规则


## [2026-09-06] - 原理图网表分析与设计规则验证

### 完成内容
- [x] 分析了 Netlist_Schematic1_2026-09-06.tel 原理图网表
- [x] 验证了设计规则符合性
- [x] 识别了潜在问题并提供了解决方案

### 设计规则验证结果
根据 Config_DesignRule_PCBv2.0_2026-09-06.json:

1. **安全间距规则**: ✓ 符合
2. **过孔尺寸规则**: ✓ 符合
3. **差分对规则**: ✓ 符合
4. **电源网络规则**: ⚠️ 需要优化

### 原理图问题分析

#### 严重问题:
1. **电源去耦电容不足**:
   - 3V3网络: 有3个100nF电容，缺少10µF电容
   - 3V3ESP网络: 有10个100nF电容，缺少10µF电容
   - 5VIN网络: 有4个100nF电容，缺少100µF电容

#### 警告问题:
2. **SPI总线共享**: GPIO42和GPIO40被显示屏和W25Q128共享
3. **I2S总线共享**: GPIO5和GPIO6被麦克风和功放共享
4. **LoRa AUX信号**: 缺少上拉电阻

### 解决方案
1. 增加电源去耦电容:
   - 3V3: 增加10µF电解电容
   - 3V3ESP: 增加10µF电解电容
   - 5VIN: 增加100µF电解电容

2. SPI信号处理:
   - 增加33Ω端接电阻
   - 注意片选信号时序

3. I2S信号处理:
   - 信号线长度匹配±2mm
   - 增加包地处理

4. LoRa信号处理:
   - AUX信号增加10kΩ上拉电阻
   - 电源入口增加EMI滤波器

### 结论
设计规则本身是合理的，但原理图需要优化电源去耦电容配置。建议在PCB布线前先修改原理图。

### 下一步
1. 用户修改原理图，增加去耦电容
2. 继续PCB布线
3. 根据软件需求优化布线规则


## [2026-09-06] - I2S总线布线指南

### 完成内容
- [x] 分析了I2S总线的信号连接
- [x] 提供了I2S布线的具体要求

### I2S信号分析
1. BCLK (GPIO5): 位时钟，最高40MHz，麦克风+功放共享
2. WS (GPIO6): 字选择，采样率(48kHz)，麦克风+功放共享
3. DIN (GPIO7): 麦克风数据，最高40MHz，仅麦克风
4. DOUT (GPIO15): 功放数据，最高40MHz，仅功放

### 布线要求
- BCLK和WS需要等长（±2mm）
- 所有I2S信号避免过孔
- 阻抗匹配50Ω
- 包地处理


## [2026-09-06] - I2S引脚功能对调分析

### 完成内容
- [x] 确认了GPIO5和GPIO6的当前功能
- [x] 分析了对调功能的可行性
- [x] 提供了具体的修改方案

### 当前配置
- GPIO5 (Pin5): I2S_BCLK (位时钟)
- GPIO6 (Pin6): I2S_WS (字选择)

### 问题分析
用户反馈GPIO5和GPIO6布线有交叉，想对调功能。

###可行性分析
1. ESP32-S3的I2S引脚可以任意配置
2. GPIO5和GPIO6都支持I2S功能
3. 对调后只需要修改代码中的引脚定义
4. 硬件接线不需要改变

### 修改方案
1. 修改 pin_config.h 中的引脚定义
2. 修改接线图文档
3. 硬件布线按新功能进行


## [2026-09-06] - GPIO5/GPIO6功能对调修改

### 完成内容
- [x] 修改 pin_config.h，对调GPIO5和GPIO6的I2S功能
- [x] 更新 wiring_diagram.md 接线图文档
- [x] 更新开发日志

### 修改详情

#### 1. pin_config.h 修改
```c
// 修改前
#define PIN_I2S_BCLK            5       /* 左边Pin5 - 位时钟 */
#define PIN_I2S_WS              6       /* 左边Pin6 - 字选择 */

// 修改后
#define PIN_I2S_BCLK            6       /* 左边Pin6 - 位时钟 */
#define PIN_I2S_WS              5       /* 左边Pin5 - 字选择 */
```

#### 2. wiring_diagram.md 修改
- Pin5: I2S_BCLK → I2S_WS
- Pin6: I2S_WS → I2S_BCLK

### 修改原因
GPIO5和GPIO6布线有交叉，对调功能可以解决交叉问题。

### 下一步
- 用户继续PCB布线
- 按新功能进行布线


## [2026-09-06] - GPIO5/GPIO6功能对调修改（完成）

### 完成内容
- [x] 修改 pin_config.h，对调GPIO5和GPIO6的I2S功能
- [x] 更新 wiring_diagram.md 接线图文档（所有相关部分）
- [x] 更新开发日志

### 修改详情

#### 1. pin_config.h 修改
```c
// 修改后
#define PIN_I2S_BCLK            6       /* 左边Pin6 - 位时钟 */
#define PIN_I2S_WS              5       /* 左边Pin5 - 字选择 */
```

#### 2. wiring_diagram.md 修改（共8处）
- 第21行: Pin5 I2S_BCLK → I2S_WS
- 第22行: Pin6 I2S_WS → I2S_BCLK
- 第156行: GPIO5 位时钟 → 字选择
- 第157行: GPIO6 字选择 → 位时钟
- 第164行: Pin5 GPIO5 (BCLK) → (WS)
- 第165行: Pin6 GPIO6 (WS) → (BCLK)
- 第177行: GPIO6 字选择 → 位时钟
- 第178行: GPIO5 位时钟 → 字选择
- 第188行: Pin5 GPIO5 (BCLK) → (WS)
- 第189行: Pin6 GPIO6 (LRC) → (BCLK)
- 第420行: GPIO5 I2S_BCLK → I2S_WS
- 第421行: GPIO6 I2S_WS → I2S_BCLK

### 修改原因
GPIO5和GPIO6布线有交叉，对调功能可以解决交叉问题。

### 下一步
- 用户继续PCB布线
- 按新功能进行布线


## [2026-09-06] - wiring_diagram.md 完整更新

### 完成内容
- [x] 更新版本号 3.4 → 3.5
- [x] 更新日期 2026-09-05 → 2026-09-06
- [x] 修正麦克风引脚表格（SCK→GPIO6，WS→GPIO5）
- [x] 修正功放引脚表格（LRC→GPIO5，BCLK→GPIO6）
- [x] 修正麦克风接线图（GPIO5→WS，GPIO6→SCK）
- [x] 修正功放接线图（GPIO5→LRC，GPIO6→BCLK）
- [x] 更新I2S接口汇总表格
- [x] 更新冲突检查表格

### 修改详情
GPIO5和GPIO6的I2S功能对调：
- GPIO5: I2S_BCLK → I2S_WS
- GPIO6: I2S_WS → I2S_BCLK

所有相关表格和接线图已同步更新。


## [2026-09-06] - 原理图网表检查

### 完成内容
- [x] 分析了 Netlist_Schematic1_2026-09-06.tel 原理图网表
- [x] 检查了电源网络、信号连接、模块配置
- [x] 确认原理图设计正确

### 检查结果

#### 电源网络
- 3V3网络: 3个去耦电容 (100nF + 10µF + 100nF) ✓
- 3V3ESP网络: 10个去耦电容 (5×10µF + 5×100nF) ✓
- 5VIN网络: 4个去耦电容 (10µF + 100nF + 100µF + 100nF) ✓
- GND网络: 50个连接 ✓

#### I2S信号
- GPIO5 (I2S_WS): ESP32 → 麦克风 + 功放 ✓
- GPIO6 (I2S_BCLK): ESP32 → 麦克风 + 功放 ✓
- GPIO7 (I2S_DIN): ESP32 → 麦克风 ✓
- GPIO15 (I2S_DOUT): ESP32 → 功放 ✓

#### SPI信号
- GPIO42 (SCLK): 显示屏 + Flash 共享 ✓
- GPIO41 (MISO): Flash ✓
- GPIO40 (MOSI): 显示屏 + Flash 共享 ✓
- 片选信号独立: GPIO48 (LCD_CS), GPIO13 (FLASH_CS) ✓

#### I2C信号
- GPIO16 (SDA): 4.7kΩ上拉电阻 ✓
- GPIO17 (SCL): 4.7kΩ上拉电阻 ✓
- GPIO18 (TOUCH_INT): 触摸中断 ✓

#### LoRa信号
- GPIO8 (M0), GPIO3 (M1): 模式控制 ✓
- GPIO46 (AUX): 状态指示 ✓
- GPIO9 (TXD), GPIO10 (RXD): UART通讯 ✓

#### 复位电路
- RST网络: 有去抖电容 ✓

### 结论
原理图设计正确，电源完整性良好，可以继续PCB布线。


## [2026-09-06] - 今日工作总结

### 完成内容
- [x] 分析了 PCB设计规则 (Config_DesignRule_PCBv2.0_2026-09-06.json)
- [x] 分析了原理图网表 (Netlist_Schematic1_2026-09-06.tel)
- [x] 验证了设计规则与原理图的符合性
- [x] 修改了GPIO5/GPIO6的I2S功能对调
- [x] 更新了pin_config.h引脚配置
- [x] 更新了wiring_diagram.md接线图文档（版本3.4→3.5）
- [x] 生成了完整的元器件采购清单
- [x] 分析了100µF电容替代方案（47µF）
- [x] 分析了10µF电容耐压问题（C5需要16V）

### 当前状态
- PCB已打样
- 元器件待采购
- 软件开发待开始

### 采购清单
- 电容: 18个 (100nF×10, 10µF×7, 47µF×1)
- 电阻: 5个 (4.7kΩ×2, 10kΩ×2, 220Ω×1)
- LED: 1个 (红色0805)
- 补购: 1个 10µF/16V电容 (用于C5)

### 下一步
1. 采购元器件
2. 等待PCB到货
3. 焊接组装
4. 开始软件开发


## [2026-09-06] - 设备树实现完善与目录结构整理

### 完成内容
- [x] 重新设计设备树API，去掉默认值参数，改为返回错误码
- [x] 添加错误处理机制：dtree_has_node()、dtree_has_property()、DTREE_CHECK_ERROR宏
- [x] 修改dtree.h和dtree.c，实现新的API接口
- [x] 整理驱动目录结构，移除无用驱动（beep、sr04）
- [x] 创建新的驱动目录：i2s_drv、i2c_drv、spi_drv、uart_drv
- [x] 创建新的模块目录：audio_module、display_module、sensor_module、touch_module、wireless_module、storage_module
- [x] 为每个驱动和模块创建基本的头文件和源文件
- [x] 更新CMakeLists.txt，添加新的组件依赖
- [x] 修复编译错误：格式说明符问题（PRId32）、缺少头文件（string.h、inttypes.h）
- [x] 整理设备树配置文件，确保所有硬件参数描述完整

### 当前状态
- 设备树API已优化，不再有默认值硬编码
- 驱动和模块目录结构已整理
- 编译基本通过，但LVGL组件缺失lvgl.h头文件
- LVGL依赖问题需要进一步解决

### 待解决问题
- LVGL库缺失，需要添加LVGL依赖或禁用LVGL组件
- 驱动和模块的具体实现仍为空（TODO）

### 下一步
1. 解决LVGL依赖问题
2. 实现各驱动的具体功能
3. 实现各模块的具体功能
4. 集成测试

## [2026-09-06] - 模块命名优化

### 完成内容
- [x] 按照文档中芯片型号重命名模块目录
- [x] display_module → st7789
- [x] sensor_module → aht30
- [x] touch_module → cst816s
- [x] wireless_module → lora
- [x] storage_module → w25q128
- [x] 更新所有CMakeLists.txt中的组件名
- [x] 更新主CMakeLists.txt中的组件路径
- [x] 更新main/CMakeLists.txt中的组件依赖

### 当前状态
- 模块命名现在与硬件芯片型号一致
- 项目结构更加清晰，易于维护
- 编译依赖关系已更新

### 待解决问题
- LVGL库缺失，需要添加LVGL依赖或禁用LVGL组件
- 各驱动和模块的具体实现仍为空（TODO）

### 下一步
1. 解决LVGL依赖问题
2. 实现各驱动的具体功能
3. 实现各模块的具体功能
4. 集成测试

## [2026-09-06] - 编译成功与LVGL临时解决方案

### 完成内容
- [x] 解决了LVGL依赖问题：创建临时lvgl.h头文件占位
- [x] 移除了main组件对lvgl的依赖
- [x] 更新了src/lvgl/CMakeLists.txt，移除对lvgl库的依赖
- [x] 项目编译成功，生成固件文件：ovs.bin (948.9 KB)
- [x] 固件签名成功：ovs_signed.bin

### 当前状态
- 项目结构已按芯片名称命名：st7789、aht30、cst816s、lora、w25q128
- 所有驱动和模块目录结构清晰
- 编译通过，固件可烧录
- LVGL功能暂时禁用，使用临时占位头文件

### 待解决问题
- LVGL库需要单独添加（当前使用临时占位头文件）
- 各驱动和模块的具体实现仍为空（TODO）

### 下一步
1. 添加真正的LVGL库（从ESP-IDF组件库或手动添加）
2. 实现各驱动的具体功能
3. 实现各模块的具体功能
4. 集成测试
5. 烧录测试

## [2026-09-07] - 实现事件总线模块 (event_bus)

### 完成内容
- [x] 设计事件总线架构方案
- [x] 实现事件类型定义 (event_bus_types.h)
  - 支持10个模块的事件类型 (系统、WiFi、传感器、触控、LoRa、UI、音频、存储、显示、Web)
  - 定义事件数据结构 (WiFi连接、温湿度、触控、LoRa等)
  - 类型安全宏 (EVENT_BUS_PUBLISH, EVENT_BUS_PUBLISH_EMPTY)
- [x] 实现内部数据结构 (event_bus_internal.h)
  - 订阅者结构、订阅句柄、队列节点、上下文
- [x] 实现公共API接口 (event_bus.h)
  - 初始化/反初始化
  - 事件发布 (event_bus_publish)
  - 事件订阅/取消订阅 (event_bus_subscribe, event_bus_unsubscribe)
  - 状态查询 (event_bus_get_status, event_bus_get_stats)
  - 调试辅助 (event_bus_print_status, event_bus_print_subscribers)
- [x] 实现核心功能 (event_bus.c)
  - 事件队列管理 (FreeRTOS队列)
  - 订阅者表管理 (互斥锁保护)
  - 事件处理任务 (独立任务，异步处理)
  - 事件分发机制
  - 统计信息收集
- [x] 实现测试用例 (event_bus_test.c)
  - 初始化/反初始化测试
  - 事件发布测试
  - 事件订阅/取消订阅测试
  - 事件处理测试
  - 多订阅者测试
  - 统计信息测试
  - 状态查询测试
  - 调试函数测试
- [x] 创建使用示例 (example_usage.c)
  - WiFi模块示例 (发布事件)
  - 传感器模块示例 (发布事件)
  - Web模块示例 (订阅事件)
  - UI模块示例 (订阅事件)
  - 完整使用流程示例
- [x] 编写详细文档 (README.md)
  - 架构说明
  - 快速开始指南
  - 事件类型列表
  - API参考
  - 配置参数
  - 内存使用说明
  - 注意事项
- [x] 更新构建配置 (CMakeLists.txt)

### 技术特点
1. **发布-订阅模式**: 模块间通过事件通信，无需直接依赖
2. **线程安全**: 使用互斥锁保护订阅者表，支持多任务并发
3. **异步处理**: 独立任务处理事件队列，不阻塞发布者
4. **类型安全**: 强类型事件定义，编译期检查
5. **资源高效**: 固定大小队列和订阅表，内存占用可预测
6. **调试友好**: 事件类型有语义名称，便于日志追踪

### 文件结构
```
components/core/event_bus/
├── CMakeLists.txt           # 构建配置
├── README.md                # 使用文档
├── event_bus.c              # 核心实现
├── event_bus.h              # 公共API
├── event_bus_internal.h     # 内部数据结构
├── event_bus_types.h        # 类型定义
├── event_bus_test.c         # 测试用例
├── event_bus_test.h         # 测试头文件
├── example_usage.c          # 使用示例
└── example_usage.h          # 示例头文件
```

### 配置参数
- 事件队列大小: 32
- 最大订阅者数量: 64
- 最大事件数据大小: 256字节
- 处理任务栈大小: 4096字节
- 处理任务优先级: 5
- 处理任务轮询间隔: 10ms

### 待解决问题
- 无

### 下一步
1. 将事件总线集成到现有模块 (WiFi、传感器、Web等)
2. 实现各驱动的具体功能
3. 实现各模块的具体功能
4. 集成测试
5. 烧录测试

## [2026-09-07] - W25Q128模块完善与Holder模块测试

### 完成内容
- [x] 修复W25Q128状态寄存器读取（改用全双工传输）
- [x] 修复w25q128_write_enable枚举类型比较错误
- [x] 修复W25Q128读取返回空数据问题（根因：CS在命令和数据间切换）
- [x] W25Q128完整测试通过：JEDEC ID、擦除、写入、读取、数据校验
- [x] Holder模块测试通过：模块注册、初始化、状态查询、错误处理
- [x] 综合测试程序（main.c）：Holder + W25Q128 全部通过
- [x] 编写W25Q128模块README.md文档

### 问题修复详情

#### 1. 枚举类型比较错误
**问题**：`w25q128_write_enable()`中`err`声明为`spi_drv_err_t`，但与`W25Q128_OK`（`w25q128_err_t`类型）比较
**修复**：将循环内的`err`改为`w25q128_err_t`类型，`spi_drv_write`结果用`spi_err`接收

#### 2. W25Q128读取返回0xFF
**问题**：使用分离的`spi_drv_write`+`spi_drv_read`两个SPI事务，CS在命令和数据之间被拉高，W25Q128退出读取模式
**修复**：改用`spi_drv_transfer`全双工传输，一次性发送cmd(1)+addr(3)+dummy(N)并接收数据，保证CS持续低电平

### 测试结果

#### Holder模块
```
alpha ready: YES (必需模块, 0错误)
beta  ready: YES (非必需模块, 0错误)
gamma ready: NO  (非必需模块, 1错误 - 预期模拟失败)
holder_print_status() 正确显示状态报告
holder_destroy() 清理正常
```

#### W25Q128模块
```
JEDEC ID: 0xEF 0x40 0x18 (Winbond W25Q128)
Flash: 16MB, 4096 sectors
擦除成功 → 写入成功 → 读取成功 → 数据校验成功 ✅
```

### 当前状态
- W25Q128模块功能完整，可正常使用
- Holder模块功能完整，可正常使用
- 串口烧录测试正常（/dev/ttyACM0）

### 待解决问题
- SPI反初始化警告："not all CSses freed"（不影响功能，后续可在spi_drv_deinit中添加设备移除）

### 文件变更
```
修改：components/modules/w25q128/w25q128.c  # 修复读取、状态寄存器、枚举比较
新增：components/modules/w25q128/README.md  # 模块文档
修改：main/main.c                           # 综合测试程序
```

### 下一步
1. 等待用户下一步需求
2. 可选：实现其他硬件模块驱动（ST7789、CST816S、AHT30、LoRa等）
3. 可选：完善spi_drv_deinit以消除CS警告

## [2026-09-07] - W25Q128改为使用设备树配置

### 完成内容
- [x] W25Q128模块改为从设备树读取配置（不再硬编码引脚）
- [x] 从 `spi.bus` 读取总线配置（SCLK/MISO/MOSI/频率/模式）
- [x] 从 `spi.flash` 读取Flash特定配置（CS引脚、工作频率）
- [x] main.c 添加 dtree_init() 调用
- [x] 更新README.md：添加设备树配置说明和性能参数文档

### 变更详情

#### w25q128.c 修改
```c
// 旧代码（硬编码）
#define W25Q128_SPI_SCLK_PIN       42
#define W25Q128_SPI_CS_PIN         13
// ...

// 新代码（设备树）
spi_drv_config_t spi_config;
spi_drv_load_config(&spi_config);  // 从 spi.bus 读取

int32_t cs_pin, flash_freq;
DTREE_INT("spi.flash", "cs_pin", &cs_pin);
DTREE_INT("spi.flash", "spi_freq_mhz", &flash_freq);
```

#### main.c 修改
```c
#include "dtree.h"

// 在 w25q128_init() 之前调用
dtree_init();
```

### 设备树配置文件
`components/dtbs/config/spi.json`
```json
{
    "bus": {
        "sclk_pin": 42,
        "miso_pin": 41,
        "mosi_pin": 40,
        "max_freq_mhz": 40,
        "mode": 0
    },
    "flash": {
        "cs_pin": 13,
        "spi_freq_mhz": 20,
        "size_mb": 16
    }
}
```

### 性能参数
| 操作 | 当前速度(20MHz) | 提升后(40MHz) | 提升后(80MHz) |
|------|-----------------|---------------|---------------|
| 顺序读取 | ~2.5 MB/s | ~5 MB/s | ~10 MB/s |
| 页写入 | ~365 KB/s | ~365 KB/s | ~365 KB/s |
| 扇区擦除 | ~89 KB/s | ~89 KB/s | ~89 KB/s |

**注意**：写入和擦除速度受Flash硬件限制，提升SPI频率主要改善读取速度。

### 文件变更
```
修改：components/modules/w25q128/w25q128.c  # 使用设备树
修改：components/modules/w25q128/README.md  # 添加设备树和性能文档
修改：main/main.c                           # 添加dtree_init()
```

### 下一步
1. 编译测试设备树配置是否正常工作
2. 可选：修改 spi.json 提升Flash频率到40MHz测试
