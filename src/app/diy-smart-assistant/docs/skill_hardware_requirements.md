# Skill硬件注意事项补充

## 重要提醒

**每次开发软件前必须先阅读 `docs/hardware_notes_for_software.md`！**

本文件补充skill中的硬件注意事项，确保软件开发时不会忽略硬件限制。

---

## 软件开发必读流程

### 第一步：阅读硬件文档
```bash
# 开发前必须执行
cat docs/hardware_notes_for_software.md
```

### 第二步：检查禁用USB/JTAG配置

#### menuconfig配置
```
Component config → ESP System Settings → Channel for console output → Custom
Component config → USB Serial JTAG → 禁用
Component config → USB OTG → 禁用
```

#### 或代码中添加
```c
// 在app_main()开头添加
esp_efuse_disable_rom_download_mode();
```

### 第三步：确认I2C设备地址
```c
// 触控屏地址
#define I2C_ADDR_TOUCH  0x15

// ATH30温湿度传感器地址
#define I2C_ADDR_ATH30  0x38
```

### 第四步：确认SPI设备片选引脚
```c
// 显示屏片选
#define GPIO_CS_LCD     21

// W25Q128 Flash片选
#define GPIO_CS_W25Q128 13
```

---

## 代码编写规范

### 1. SPI设备切换规范
```c
// ✅ 正确示例
void spi_select_lcd() {
    gpio_set_level(GPIO_CS_W25Q128, 1);  // 先释放其他设备
    gpio_set_level(GPIO_CS_LCD, 0);      // 再选中目标设备
}

void spi_select_w25q128() {
    gpio_set_level(GPIO_CS_LCD, 1);      // 先释放其他设备
    gpio_set_level(GPIO_CS_W25Q128, 0);  // 再选中目标设备
}

// ❌ 错误示例
void spi_select_lcd_wrong() {
    gpio_set_level(GPIO_CS_LCD, 0);      // 忘记释放其他设备
}
```

### 2. I2C通信错误处理规范
```c
// ✅ 正确示例：添加重试机制
esp_err_t i2c_read_with_retry(uint8_t addr, uint8_t reg, uint8_t *data, size_t len, int max_retries) {
    esp_err_t ret;
    for (int i = 0; i < max_retries; i++) {
        ret = i2c_read_device(addr, reg, data, len);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "I2C read failed, retry %d/%d", i + 1, max_retries);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGE(TAG, "I2C read failed after %d retries", max_retries);
    return ret;
}

// ❌ 错误示例：无错误处理
void i2c_read_wrong() {
    i2c_read_device(addr, reg, data, len);  // 忘记检查返回值
}
```

### 3. I2S配置规范
```c
// ✅ 正确示例：麦克风和功放使用相同采样率
#define SAMPLE_RATE 16000
#define BITS_PER_SAMPLE 16

i2s_config_t i2s_config = {
    .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX,
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = BITS_PER_SAMPLE,
    // ...
};

// ❌ 错误示例：采样率不一致
void i2s_config_wrong() {
    // 麦克风配置
    mic_config.sample_rate = 16000;
    // 功放配置
    amp_config.sample_rate = 44100;  // 错误！应该一致
}
```

- [ ] 测试W25Q128读写
- [ ] 测试SPI设备切换

### I2C测试
- [ ] 测试触控屏通信
- [ ] 测试ATH30读取
- [ ] 测试I2C设备扫描

### I2S测试
- [ ] 测试麦克风录音
- [ ] 测试功放播放
- [ ] 测试音频同步

### LoRa测试
- [ ] 测试LoRa发送
- [ ] 测试LoRa接收
- [ ] 测试模式切换

### 电源测试

- [ ] 测试低电量保护
- [ ] 测试续航时间

---

## 常见问题排查

### 问题1：SPI设备无响应
**排查步骤**：
1. 检查CS引脚状态
2. 检查SPI时钟频率
3. 检查设备电源

**调试代码**：
```c
printf("CS_LCD: %d\n", gpio_get_level(GPIO_CS_LCD));
printf("CS_W25Q128: %d\n", gpio_get_level(GPIO_CS_W25Q128));
```

### 问题2：I2C通信失败
**排查步骤**：
1. 检查设备地址
2. 检查上拉电阻
3. 扫描I2C总线

**调试代码**：
```c
// 扫描I2C设备
for (int addr = 0; addr < 128; addr++) {
    if (i2c_test_address(addr) == ESP_OK) {
        printf("Found device at: 0x%02X\n", addr);
    }
}
```

### 问题3：音频播放异常
**排查步骤**：
1. 检查I2S配置
2. 检查采样率一致性
3. 检查DMA缓冲区

**调试代码**：
```c
i2s_status_t status;
i2s_get_status(I2S_NUM_0, &status);
printf("I2S status: %d\n", status);
```

---

## 相关文档

- `docs/hardware_notes_for_software.md` - 硬件详细注意事项（必读）
- `hardware/wiring_diagram.md` - 硬件接线图
- `logs/development_log.md` - 开发日志
- `docs/` - 其他技术文档

---

**最后更新**：2026-09-03
**维护者**：软件团队
**版本**：1.0
