# 硬件注意事项（软件开发必读）

## 重要提醒

**每次开发软件前必须阅读本文档！**

本文档从硬件角度出发，列出软件开发时必须注意的事项，避免因为不了解硬件而导致的bug。

---

## 一、ESP32-S3-R16N8 核心板信息

### 核心板规格
- **型号**: ESP32-S3-R16N8
- **排针**: 22×2 = 44Pin，2.54mm间距
- **Flash**: 16MB
- **PSRAM**: 8MB
- **双核**: Xtensa LX7 240MHz

### 供电方式
```
电源模块 5V → 核心板 VIN 引脚 → 板载LDO → 3.3V
电源模块 GND → 核心板 GND
```

---

## 二、引脚分配总表（v3.3）

### 左边从上往下（22个引脚）

| Pin | GPIO | 功能 | 模块 | 说明 |
|-----|------|------|------|------|
| 1 | 3V3 | 电源 | - | 3.3V输出 |
| 2 | 3V3 | 电源 | - | 3.3V输出 |
| 3 | RST | 复位 | - | 低电平有效 |
| 4 | GPIO4 | 空闲 | - | 未使用 |
| 5 | GPIO5 | I2S_BCLK | 麦克风/功放 | I2S时钟 |
| 6 | GPIO6 | I2S_WS | 麦克风/功放 | I2S字选择 |
| 7 | GPIO7 | I2S_DIN | 麦克风 | 麦克风数据 |
| 8 | GPIO15 | I2S_DOUT | 功放 | 功放数据 |
| 9 | GPIO16 | I2C_SDA | 触控/ATH30 | I2C数据 |
| 10 | GPIO17 | I2C_SCL | 触控/ATH30 | I2C时钟 |
| 11 | GPIO18 | 触控_INT | 触控 | 触控中断 |
| 12 | GPIO8 | 空闲 | - | 未使用 |
| 13 | GPIO3 | 空闲 | - | 未使用 |
| 14 | GPIO46 | 空闲 | - | 未使用 |
| 15 | GPIO9 | 空闲 | - | 未使用 |
| 16 | GPIO10 | 空闲 | - | 未使用 |
| 17 | GPIO11 | 空闲 | - | 未使用 |
| 18 | GPIO12 | 空闲 | - | 未使用 |
| 19 | GPIO13 | SPI_CS | W25Q128 | Flash片选 |
| 20 | GPIO14 | LED | 系统 | 状态指示灯 |
| 21 | VIN | 电源 | - | 5V输入 |
| 22 | GND | 地 | - | 接地 |

### 右边从上往下（22个引脚）

| Pin | GPIO | 功能 | 模块 | 说明 |
|-----|------|------|------|------|
| 1 | GND | 地 | - | 接地 |
| 2 | GPIO43 | UART0_TX | 调试 | 串口发送 |
| 3 | GPIO44 | UART0_RX | 调试 | 串口接收 |
| 4 | GPIO1 | LoRa_M0 | LoRa | 模式选择0 |
| 5 | GPIO2 | LoRa_M1 | LoRa | 模式选择1 |
| 6 | GPIO42 | SPI_SCLK | 显示屏/W25Q128 | SPI时钟 |
| 7 | GPIO41 | SPI_MISO | W25Q128 | SPI数据输入 |
| 8 | GPIO40 | SPI_MOSI | 显示屏/W25Q128 | SPI数据输出 |
| 9 | GPIO39 | LCD_RST | 显示屏 | 显示屏复位 |
| 10 | GPIO38 | LCD_BL | 显示屏 | 背光控制 |
| 11 | GPIO37 | LoRa_AUX | LoRa | 状态指示 |
| 12 | GPIO36 | LoRa_TXD | LoRa | 串口发送 |
| 13 | GPIO35 | LoRa_RXD | LoRa | 串口接收 |
| 14 | GPIO0 | 空闲 | - | BOOT引脚(未使用) |
| 15 | GPIO45 | 空闲 | - | 未使用 |
| 16 | GPIO48 | LCD_CS | 显示屏 | 显示屏片选 |
| 17 | GPIO47 | LCD_DC | 显示屏 | 数据/命令选择 |
| 18 | GPIO21 | 空闲 | - | 未使用 |
| 19 | GPIO20 | 空闲 | - | 未使用 |
| 20 | GPIO19 | 空闲 | - | 未使用 |
| 21 | GND | 地 | - | 接地 |
| 22 | GND | 地 | - | 接地 |

### 空闲引脚（13个）
```
左边：GPIO3, GPIO4, GPIO8, GPIO9, GPIO10, GPIO11, GPIO12, GPIO46
右边：GPIO0(BOOT), GPIO19, GPIO20, GPIO21, GPIO45
```

---

## 三、总线共用说明

### 1. SPI总线共用（显示屏 + W25Q128）

**共用引脚**：
- GPIO42: SCLK（时钟）
- GPIO40: MOSI（主机输出）

**独立引脚**：
- GPIO41: MISO（W25Q128数据输出）
- GPIO48: CS_LCD（显示屏片选）
- GPIO13: CS_W25Q128（Flash片选）
- GPIO47: DC_LCD（显示屏数据/命令）
- GPIO39: RST_LCD（显示屏复位）
- GPIO38: BL_LCD（显示屏背光）

**软件注意事项**：
```c
// 1. 同一时刻只能操作一个SPI设备
// 2. 切换设备时需要先拉高前一个设备的CS，再拉低新设备的CS
// 3. SPI时钟频率需要取两个设备的最小值

// CS引脚定义
#define PIN_CS_LCD      48
#define PIN_CS_W25Q128  13

// 切换SPI设备
void spi_select_device(int device) {
    gpio_set_level(PIN_CS_LCD, 1);
    gpio_set_level(PIN_CS_W25Q128, 1);
    
    if (device == 0) {
        gpio_set_level(PIN_CS_LCD, 0);      // 选中显示屏
    } else {
        gpio_set_level(PIN_CS_W25Q128, 0);  // 选中Flash
    }
}
```

**常见错误**：
- ❌ 同时操作两个SPI设备
- ❌ 忘记释放前一个设备的CS
- ❌ 使用不同时钟频率导致通信失败

---

### 2. I2S总线共用（麦克风 + 功放）

**共用引脚**：
- GPIO5: BCLK（位时钟）
- GPIO6: WS（字选择）

**独立引脚**：
- GPIO7: DIN（麦克风数据输入）
- GPIO15: DOUT（功放数据输出）

**软件注意事项**：
```c
// 1. 麦克风和功放可以同时工作（全双工）
// 2. 时钟配置需要同时满足两个设备的要求
// 3. 数据格式（采样率、位宽）需要一致

// I2S配置示例
i2s_config_t i2s_config = {
    .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX,
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
};
```

**常见错误**：
- ❌ 麦克风和功放使用不同的采样率
- ❌ 忘记配置DMA缓冲区导致音频卡顿
- ❌ 时钟配置错误导致音频失真

---

### 3. I2C总线共用（触控 + ATH30）

**共用引脚**：
- GPIO16: SDA（数据线）
- GPIO17: SCL（时钟线）

**独立引脚**：
- GPIO18: INT（触控中断）

**设备地址**：
- 触控屏：0x15（CST816S默认地址）
- ATH30：0x38（ATH30默认地址）

**软件注意事项**：
```c
// I2C设备地址定义
#define I2C_ADDR_TOUCH  0x15
#define I2C_ADDR_ATH30  0x38

// I2C读写示例
esp_err_t i2c_read_device(uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device_addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}
```

**常见错误**：
- ❌ 设备地址错误导致通信失败
- ❌ 忘记处理I2C总线错误（NACK、超时）
- ❌ 多个任务同时操作I2C导致冲突

---

## 四、禁用USB/JTAG说明

### 必须禁用的功能
- USB Serial/JTAG
- USB OTG

### 受影响的引脚（共8个）
| GPIO | 原功能 | 禁用后用途 |
|------|--------|------------|
| GPIO5 | JTAG MTCK | I2S BCLK |
| GPIO6 | JTAG MTDO | I2S WS |
| GPIO15 | USB JTAG D+ | I2S DOUT |
| GPIO19 | USB OTG D- | 空闲 |
| GPIO20 | USB OTG D+ | 空闲 |
| GPIO47 | USB Serial D+ | 显示屏DC |
| GPIO48 | USB Serial D- | 显示屏CS |

### 软件配置（必须）

#### 方法1：menuconfig配置
```
Component config → ESP System Settings → Channel for console output → Custom
Component config → USB Serial JTAG → 禁用
Component config → USB OTG → 禁用
```

#### 方法2：代码中添加
```c
// 在app_main()开头添加
esp_efuse_disable_rom_download_mode();
```

### 影响
- ✅ COM接口（UART0）正常工作（GPIO43/44）
- ❌ USB接口无法使用
- ✅ 串口烧录和监控不受影响

---

## 五、模块通信协议

### 1. 电容触控显示屏（SPI + I2C）

**显示接口（SPI）**：
- 分辨率：320x240
- 颜色深度：16bit (RGB565)
- 通信频率：最高40MHz
- CS引脚：GPIO48
- DC引脚：GPIO47
- RST引脚：GPIO39
- BL引脚：GPIO38

**触控接口（I2C）**：
- 设备地址：0x15
- 中断引脚：GPIO18
- 复位：直接接3.3V（不使用复位功能）

### 2. W25Q128 Flash（SPI）

**参数**：
- 容量：128MB
- 页大小：256字节
- 扇区大小：4KB
- 块大小：64KB
- CS引脚：GPIO13

**软件注意事项**：
```c
// 擦除操作必须按扇区进行
// 写入前必须先擦除
// 读取可以任意地址任意长度
```

### 3. INMP441 麦克风（I2S）

**参数**：
- 采样率：最高44.1kHz
- 位宽：24bit
- 信噪比：61dB

**引脚**：
- SCK: GPIO5 (BCLK)
- WS: GPIO6
- SD: GPIO7 (DIN)
- L/R: GND（左声道）

**软件处理**：
```c
// I2S立体声数据：buffer[0]=左声道, buffer[1]=右声道
int16_t i2s_buffer[2];
int16_t mic_data = i2s_buffer[0];  // 只使用左声道
```

### 4. MAX98357A 功放（I2S）

**参数**：
- 输出功率：3.2W
- 供电电压：5V
- 增益：默认15dB

**引脚**：
- BCLK: GPIO5 (与麦克风共用)
- LRC: GPIO6 (与麦克风共用)
- DIN: GPIO15 (DOUT)
- GAIN: GND (15dB)
- SD: 3.3V (使能)

### 5. LR22 LoRa（UART）

**参数**：
- 工作频率：433MHz
- 通信距离：3-5km
- 串口波特率：9600（默认）

**引脚定义**：
- M0: GPIO1 (右边Pin4)
- M1: GPIO2 (右边Pin5)
- AUX: GPIO37 (右边Pin11)
- TXD: GPIO36 (右边Pin12)
- RXD: GPIO35 (右边Pin13)

**工作模式**：
| M1 | M0 | 模式 |
|----|----|----|
| 0 | 0 | 高时效模式（默认） |
| 0 | 1 | AT模式 |
| 1 | 0 | 空中唤醒模式 |
| 1 | 1 | 休眠模式 |

**AUX引脚**：
- 高电平：数据发送中/接收中/模式切换中
- 低电平：数据发送完成/接收完成/模式切换完成

### 6. ATH30 温湿度传感器（I2C）

**参数**：
- 设备地址：0x38
- 温度范围：-40~80°C
- 湿度范围：0~100%RH
- 精度：±0.3°C / ±2%RH

**引脚**：
- SDA: GPIO16
- SCL: GPIO17

---

## 六、电源注意事项

### 供电分配
- **3.3V**：屏幕、W25Q128、麦克风、LoRa、ATH30、触摸
- **5V**：ESP32-S3（VIN）、MAX98357A功放

### 电流需求
| 设备 | 电流 |
|------|------|
| ESP32-S3 | ~100mA |
| 显示屏 | ~50mA |
| W25Q128 | ~10mA |
| 麦克风 | ~3mA |
| LoRa | ~40mA (发射时) |
| ATH30 | ~1mA |
| 触摸 | ~5mA |
| 功放 | ~500mA (最大) |
| **总计** | **~700mA** |

---

## 七、开发流程检查清单

### 软件开发前
- [ ] 阅读本文档
- [ ] 确认禁用USB/JTAG配置
- [ ] 确认I2C设备地址
- [ ] 确认SPI设备片选引脚

### 代码编写时
- [ ] SPI设备切换时正确操作CS引脚
- [ ] I2C通信添加错误处理和重试
- [ ] I2S配置采样率一致

### 测试验证时
- [ ] 测试SPI设备切换是否正常
- [ ] 测试I2C设备通信是否稳定
- [ ] 测试音频录制和播放是否正常
- [ ] 测试LoRa通信是否正常
- [ ] 测试触控响应是否正常

---

## 八、常见问题排查

### 1. SPI设备无响应
**可能原因**：
- CS引脚未正确拉低
- 时钟频率过高
- 设备未上电

**排查方法**：
```c
printf("CS_LCD: %d\n", gpio_get_level(48));
printf("CS_W25Q128: %d\n", gpio_get_level(13));
```

### 2. I2C设备通信失败
**可能原因**：
- 设备地址错误
- 上拉电阻缺失
- 总线被占用

**排查方法**：
```c
// 扫描I2C设备
for (int addr = 0; addr < 128; addr++) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    if (ret == ESP_OK) {
        printf("Found device at address: 0x%02X\n", addr);
    }
    i2c_cmd_link_delete(cmd);
}
```

### 3. 音频播放异常
**可能原因**：
- I2S配置错误
- DMA缓冲区不足
- 时钟配置错误

### 4. LoRa通信失败
**可能原因**：
- 波特率配置错误
- M0/M1模式设置错误
- 天线未连接

---

## 九、参考资料

### 官方文档
- ESP32-S3技术参考手册
- ESP-IDF编程指南
- 各模块数据手册

### 项目文档
- `hardware/wiring_diagram.md` - 硬件接线图
- `logs/development_log.md` - 开发日志
- `components/dtbs/config/ovs.dtb.json` - 设备树引脚配置（单棵树）
- `include/dtree.h` - 设备树 API

---

**最后更新**：2026-09-05 (v3.4引脚分配)
**版本**：2.0
