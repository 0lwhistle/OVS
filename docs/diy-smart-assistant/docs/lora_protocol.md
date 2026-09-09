# LoRa通讯协议设计

## AT指令表

指令	功能	说明（默认值）
+++	进入或退出AT命令模式	上电默认为传输模式
AT	测试指令	用于测试串口
AT+RESET	软件重启	
AT+DEFAULT	恢复出厂设置	
AT+BAUD	设置/查询串口波特率	默认：3（9600）
AT+PARI	设置/查询串口校验位	默认：0（无校验）
AT+HELP	查询配置信息	
AT+LEVEL	设置/查询模块空中速率和通讯距离	默认：2
AT+MODE	设置/查询传输模式	默认：0（透明传输）
AT+SLEEP	设置/查询工作模式	默认：2（高时效模式）
AT+SWITCH	设置/查询硬件控制引脚状态	默认：0（关闭）
AT+CHANNEL	设置/查询工作信道	默认：00
AT+MAC	设置/查询设备地址	默认：ff,ff
AT+OPENKEY	设置/查询模块密钥开关	默认：1（打开）
AT+KEY	设置模块密钥	默认：12345
AT+PACKET	设置/查询分包长度	默认：3（230bytes）
AT+DRSSI	设置/查询数据包RSSI	默认：0（关闭）
AT+POWE	设置/查询发射功率	默认：22
AT+LBT	设置/查询LBT状态	默认：0（关闭）
AT+LRSSI	设置/查询LBT监听阀值	默认：-100
AT+ERSSI	查询当前信道噪声水平	

# DX-LR22 LoRa模组 AT指令集

## 命令格式说明

```
AT+Command<param1,param2,...><CR><LF>
```

- 所有指令以 `AT` 开头，以 `<CR><LF>` 结束（`\r` = 0x0D，`\n` = 0x0A）
- 所有AT命令字符均为英文大写
- 多个参数以逗号`,`隔开
- 指令执行成功返回 `OK`，失败返回 `ERROR=<错误码>`

## 回应格式说明

```
+Indication=<param1,param2,...><CR><LF>
```

- 回应以 `+` 开头，以 `<CR><LF>` 结束
- `=` 后面为回应参数

---

## AT指令一览表

| 指令 | 功能 | 参数 | 默认值 | 查询回应 | 设置回应 |
|------|------|------|--------|----------|----------|
| `+++` | 进入/退出AT命令模式 | 无 | — | `Entry AT` 或 `Exit AT` | — |
| `AT` | 测试指令 | 无 | — | `OK` | — |
| `AT+RESET` | 软件重启 | 无 | — | `OK` `Power On` | — |
| `AT+DEFAULT` | 恢复出厂设置 | 无 | — | `OK` `Power On` | — |
| `AT+BAUD` | 设置/查询串口波特率 | `1`=2400, `2`=4800, `3`=9600, `4`=19200, `5`=38400, `6`=57600, `7`=115200 | `3` (9600) | `+BAUD=<baud>` | `+BAUD=<baud>` `OK` |
| `AT+PARI` | 设置/查询串口校验位 | `0`=无校验, `1`=奇校验, `2`=偶校验 | `0` (无校验) | `+PARI=<param>` | `+PARI=<param>` `OK` |
| `AT+HELP` | 查询配置信息 | 无 | — | 返回多项配置参数 | — |
| `AT+LEVEL` | 设置/查询空中速率和通讯距离 | `0`~`7`（8个档位） | `2` | `+LEVEL=<param>` | `+LEVEL=<param>` `OK` |
| `AT+MODE` | 设置/查询传输模式 | `0`=透明传输, `1`=定点传输, `2`=广播传输 | `0` (透明传输) | `+MODE=<param>` | `+MODE=<param>` `OK` |
| `AT+SLEEP` | 设置/查询工作模式 | `0`=休眠模式, `1`=空中唤醒模式, `2`=高时效模式 | `2` (高时效模式) | `+SLEEP=<param>` | `+SLEEP=<param>` `OK` |
| `AT+SWITCH` | 设置/查询硬件控制引脚状态 | `0`=关闭, `1`=打开 | `0` (关闭) | `+SWITCH=<param>` | `+SWITCH=<param>` `OK` |
| `AT+CHANNEL` | 设置/查询工作信道 | `00`~`63`（十六进制） | 433T22D: `00`<br>900T22D: `41` | `+CHANNEL=<param>` | `+CHANNEL=<param>` `OK` |
| `AT+MAC` | 设置/查询设备地址 | 两字节十六进制（如 `ff,ff`） | `ff,ff` | `+MAC=<param>,<param>` | `+MAC=<param>,<param>` `OK` |
| `AT+OPENKEY` | 设置/查询模块密钥开关 | `0`=关闭, `1`=打开 | `1` (打开) | `+OPENKEY=<param>` | `+OPENKEY=<param>` `OK` |
| `AT+KEY` | 设置模块密钥 | `0`~`65535` | `12345` | 不可查询 | `+KEY=<param>` `OK` |
| `AT+PACKET` | 设置/查询分包长度 | `0`=32bytes, `1`=64bytes, `2`=128bytes, `3`=230bytes | `3` (230bytes) | `+PACKET=<param>` | `+PACKET=<param>` `OK` |
| `AT+DRSSI` | 设置/查询数据包RSSI | `0`=关闭, `1`=打开 | `0` (关闭) | `+DRSSI=<param>` | `+DRSSI=<param>` `OK` |
| `AT+POWE` | 设置/查询发射功率 | `0`~`22` dBm（整数值） | `22` | `+POWE=<param>` | `+POWE=<param>` `OK` |
| `AT+LBT` | 设置/查询LBT状态 | `0`=关闭, `1`=打开 | `0` (关闭) | `+LBT=<param>` | `+LBT=<param>` `OK` |
| `AT+LRSSI` | 设置/查询LBT监听阈值 | `-255`~`0` | `-100` | `+LRSSI=<param>` | `+LRSSI=<param>` `OK` |
| `AT+ERSSI` | 查询当前信道噪声水平 | 无 | — | `+ERSSI=<param>` | — |
| `AT+IQ` | 设置/查询IQ翻转（仅900T22D） | `0`=关闭, `1`=开启 | `1` (开启) | `+IQ=<param>` | `+IQ=<param>` `OK` |
| `AT+CRC` | 设置/查询CRC校验（仅900T22D） | `0`=关闭, `1`=开启 | `1` (开启) | `+CRC=<param>` | `+CRC=<param>` `OK` |

---

## 错误码

| 错误码 | 说明 |
|--------|------|
| `104` | 无效指令 |
| `105` | 无效参数 |
| `106` | 其他错误 |

---

## 重要说明

1. **重启生效**：以下指令设置后需发送 `AT+RESET` 重启生效：`AT+BAUD`、`AT+PARI`、`AT+LEVEL`、`AT+MODE`、`AT+SLEEP`、`AT+SWITCH`、`AT+CHANNEL`、`AT+MAC`、`AT+OPENKEY`、`AT+KEY`、`AT+PACKET`、`AT+DRSSI`、`AT+POWE`、`AT+LBT`、`AT+LRSSI`、`AT+IQ`、`AT+CRC`

2. **退出AT模式**：发送 `+++` 退出AT命令模式时会自动复位

3. **`+++` 指令掉电不保存**

4. **`AT+KEY` 不可查询**，只能设置

5. **`AT+ERSSI` 只可查询**，不可设置

## 概述

本文档定义了ESP32-S3桌面智能助手的LoRa通讯协议，支持实时对讲和留言功能。

## 协议架构

### 1. 物理层参数
- **频率**: 433MHz（国内免许可频段）
- **扩频因子**: SF7（近距离）/ SF12（远距离）
- **带宽**: 125kHz
- **编码率**: 4/5
- **发射功率**: 20dBm（最大）
- **预期距离**: 城市1-3km，郊区3-5km

### 2. 数据链路层

#### 帧格式
```
┌────────┬────────┬────────┬────────┬────────┬────────┬────────┐
│ Preamble │ Sync Word │ Header │ Length │ Payload │ CRC │ Tail │
│ 8字节   │ 4字节    │ 1字节  │ 1字节  │ N字节  │ 2字节 │ 1字节 │
└────────┴────────┴────────┴────────┴────────┴────────┴────────┘
```

#### 帧类型定义
```c
typedef enum {
    FRAME_TYPE_BEACON     = 0x01,  // 信标帧（设备发现）
    FRAME_TYPE_DATA       = 0x02,  // 数据帧（对讲音频）
    FRAME_TYPE_MSG        = 0x03,  // 消息帧（留言）
    FRAME_TYPE_ACK        = 0x04,  // 确认帧
    FRAME_TYPE_NACK       = 0x05,  // 否定确认
    FRAME_TYPE_HEARTBEAT  = 0x06,  // 心跳帧
    FRAME_TYPE_CONTROL    = 0x07,  // 控制帧（开始/停止对讲）
} frame_type_t;
```

#### 设备地址
```c
typedef struct {
    uint8_t network_id;      // 网络ID（0-255）
    uint16_t device_id;      // 设备ID（0-65535）
} device_addr_t;
```

### 3. 网络层

#### 网络拓扑
```
┌─────────────────────────────────────────────────────────┐
│                    LoRa网络拓扑                          │
├─────────────────────────────────────────────────────────┤
│                                                         │
│    设备A ──────────────────────────── 设备B             │
│      │                                │                 │
│      │                                │                 │
│    设备C ──────────────────────────── 设备D             │
│                                                         │
│    * 支持点对点通讯                                      │
│    * 支持广播通讯                                        │
│    * 支持留言转发                                        │
└─────────────────────────────────────────────────────────┘
```

#### 设备发现
```c
// 信标帧结构
typedef struct {
    uint8_t frame_type;        // FRAME_TYPE_BEACON
    uint16_t device_id;        // 发送设备ID
    uint8_t network_id;        // 网络ID
    uint8_t device_name[16];   // 设备名称
    uint8_t battery_level;     // 电池电量
    uint8_t status;            // 设备状态
} beacon_frame_t;
```

## 通讯流程

### 1. 设备发现流程
```
设备A                    设备B
   │                        │
   │──── Beacon (广播) ────→│
   │                        │
   │←──── Beacon (回复) ────│
   │                        │
   │──── ACK ──────────────→│
   │                        │
   │    设备发现完成         │
```

### 2. 实时对讲流程
```
设备A                    设备B
   │                        │
   │──── Control (请求对讲) →│
   │                        │
   │←──── ACK (同意) ────────│
   │                        │
   │──── Data (音频帧1) ───→│
   │←──── ACK ──────────────│
   │                        │
   │──── Data (音频帧2) ───→│
   │←──── ACK ──────────────│
   │                        │
   │         ...            │
   │                        │
   │──── Control (结束对讲) →│
   │←──── ACK ──────────────│
   │                        │
```

### 3. 留言发送流程
```
设备A                    设备B
   │                        │
   │──── Msg (留言头) ─────→│
   │←──── ACK ──────────────│
   │                        │
   │──── Msg (留言数据1) ──→│
   │←──── ACK ──────────────│
   │                        │
   │──── Msg (留言数据2) ──→│
   │←──── ACK ──────────────│
   │                        │
   │──── Msg (留言结束) ───→│
   │←──── ACK ──────────────│
   │                        │
   │    留言发送完成         │
```

### 4. 留言接收流程
```
设备A                    设备B
   │                        │
   │←──── Msg (留言头) ─────│
   │──── ACK ──────────────→│
   │                        │
   │←──── Msg (留言数据1) ──│
   │──── ACK ──────────────→│
   │                        │
   │←──── Msg (留言数据2) ──│
   │──── ACK ──────────────→│
   │                        │
   │←──── Msg (留言结束) ───│
   │──── ACK ──────────────→│
   │                        │
   │    保存留言到W25Q128 Flash       │
   │    提示用户有新留言     │
```

## 数据结构定义

### 1. 音频数据帧
```c
typedef struct {
    uint8_t frame_type;        // FRAME_TYPE_DATA
    uint16_t sender_id;        // 发送者ID
    uint16_t receiver_id;      // 接收者ID
    uint16_t sequence;         // 序列号
    uint8_t audio_format;      // 音频格式（PCM/ADPCM）
    uint8_t sample_rate;       // 采样率
    uint8_t channels;          // 通道数
    uint8_t data[48];          // 音频数据（48字节/帧）
} audio_frame_t;
```

### 2. 留消息帧
```c
typedef struct {
    uint8_t frame_type;        // FRAME_TYPE_MSG
    uint16_t sender_id;        // 发送者ID
    uint16_t receiver_id;      // 接收者ID
    uint32_t message_id;       // 消息ID
    uint8_t message_type;      // 消息类型（文本/音频）
    uint16_t total_length;     // 总长度
    uint16_t offset;           // 偏移量
    uint8_t data[48];          // 消息数据
} message_frame_t;
```

### 3. 控制帧
```c
typedef struct {
    uint8_t frame_type;        // FRAME_TYPE_CONTROL
    uint16_t sender_id;        // 发送者ID
    uint16_t receiver_id;      // 接收者ID
    uint8_t command;           // 命令类型
    uint8_t parameter;         // 参数
} control_frame_t;
```

## 命令定义

### 控制命令
```c
typedef enum {
    CMD_START_CALL      = 0x01,  // 开始对讲
    CMD_STOP_CALL       = 0x02,  // 停止对讲
    CMD_ACCEPT_CALL     = 0x03,  // 接受对讲
    CMD_REJECT_CALL     = 0x04,  // 拒绝对讲
    CMD_MUTE            = 0x05,  // 静音
    CMD_UNMUTE          = 0x06,  // 取消静音
    CMD_SET_VOLUME      = 0x07,  // 设置音量
    CMD_GET_STATUS      = 0x08,  // 获取状态
    CMD_SET_NETWORK     = 0x09,  // 设置网络
    CMD_RESET           = 0x0A,  // 重置设备
} command_t;
```

## 错误处理

### 1. 超时机制
- **ACK超时**: 500ms
- **重传次数**: 3次
- **心跳间隔**: 30秒

### 2. 错误检测
- **CRC校验**: CRC-16
- **序列号检查**: 防止重复帧
- **长度检查**: 防止缓冲区溢出

### 3. 错误恢复
```c
// 重传机制
if (send_frame(frame) != ESP_OK) {
    for (int retry = 0; retry < MAX_RETRY; retry++) {
        if (send_frame(frame) == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY));
    }
}
```

## 音频编码

### 1. 音频参数
- **采样率**: 8kHz（语音）
- **位深**: 16bit
- **通道**: 单声道
- **编码**: PCM / ADPCM

### 2. 帧大小计算
```
采样率: 8000Hz
位深: 16bit = 2字节
通道: 1
帧时长: 20ms

每帧采样数 = 8000 × 0.02 = 160采样
每帧字节数 = 160 × 2 = 320字节

使用ADPCM压缩: 320 / 4 = 80字节
LoRa帧最大负载: 48字节

解决方案: 分片传输
每帧分片数 = 80 / 48 ≈ 2片
```

### 3. ADPCM编码
```c
// ADPCM编码表
static const int16_t adpcm_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    // ... 省略
};

// 编码函数
int adpcm_encode(const int16_t *input, uint8_t *output, int samples) {
    // 实现ADPCM编码
}
```

## 留言存储格式

### 1. 文件命名规则
```
/loRa_messages/
├── inbox/                    # 收件箱
│   ├── 20260903_143022_001.msg
│   ├── 20260903_143022_002.msg
│   └── ...
├── outbox/                   # 发件箱
│   ├── 20260903_143022_001.msg
│   └── ...
└── draft/                    # 草稿箱
    └── ...
```

### 2. 消息文件格式
```c
// 消息头
typedef struct {
    uint32_t magic;            // 魔数 0x4C4F5241 ("LORA")
    uint16_t version;          // 版本号
    uint16_t header_size;      // 头大小
    uint32_t message_id;       // 消息ID
    uint16_t sender_id;        // 发送者ID
    uint16_t receiver_id;      // 接收者ID
    uint32_t timestamp;        // 时间戳
    uint8_t message_type;      // 消息类型
    uint8_t status;            // 消息状态
    uint32_t data_length;      // 数据长度
    uint8_t reserved[8];       // 保留
} message_header_t;

// 消息状态
typedef enum {
    MSG_STATUS_UNREAD    = 0x00,  // 未读
    MSG_STATUS_READ      = 0x01,  // 已读
    MSG_STATUS_SENT      = 0x02,  // 已发送
    MSG_STATUS_FAILED    = 0x03,  // 发送失败
    MSG_STATUS_DRAFT     = 0x04,  // 草稿
} message_status_t;
```

## 安全考虑

### 1. 网络隔离
- **网络ID**: 不同网络ID的设备不通讯
- **设备白名单**: 可配置设备白名单

### 2. 数据加密
- **AES-128**: 可选的数据加密
- **密钥交换**: 预共享密钥

### 3. 防重放攻击
- **序列号**: 递增序列号
- **时间戳**: 时间戳校验

## 性能优化

### 1. 传输优化
- **自适应速率**: 根据距离调整扩频因子
- **前向纠错**: FEC提高可靠性
- **压缩**: 音频数据压缩

### 2. 功耗优化
- **休眠模式**: 空闲时进入休眠
- **按需唤醒**: 接收到数据时唤醒
- **功率控制**: 动态调整发射功率

### 3. 内存优化
- **环形缓冲区**: 音频数据缓冲
- **内存池**: 固定大小内存块
- **零拷贝**: 减少数据拷贝

## 测试用例

### 1. 基础通讯测试
```c
void test_basic_communication(void) {
    // 测试设备发现
    // 测试数据发送接收
    // 测试错误处理
}
```

### 2. 对讲功能测试
```c
void test_intercom_function(void) {
    // 测试对讲建立
    // 测试音频传输
    // 测试对讲结束
}
```

### 3. 留言功能测试
```c
void test_message_function(void) {
    // 测试留言发送
    // 测试留言接收
    // 测试留言存储
}
```

### 4. 压力测试
```c
void test_stress(void) {
    // 测试长时间运行
    // 测试高并发
    // 测试内存泄漏
}
```

## 调试工具

### 1. 串口调试
```c
#define LORA_DEBUG_ENABLE 1

#if LORA_DEBUG_ENABLE
#define LORA_DEBUG(fmt, ...) ESP_LOGI("LORA", fmt, ##__VA_ARGS__)
#else
#define LORA_DEBUG(fmt, ...)
#endif
```

### 2. 协议分析
```c
void dump_frame(const uint8_t *frame, int len) {
    ESP_LOGI("LORA", "Frame dump:");
    for (int i = 0; i < len; i++) {
        printf("%02X ", frame[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");
}
```

### 3. 统计信息
```c
typedef struct {
    uint32_t tx_count;         // 发送计数
    uint32_t rx_count;         // 接收计数
    uint32_t error_count;      // 错误计数
    uint32_t retry_count;      // 重试计数
    int16_t rssi;              // 信号强度
    float snr;                 // 信噪比
} lora_stats_t;
```

## 总结

本协议设计支持：
- ✅ 设备自动发现
- ✅ 实时对讲（低延迟）
- ✅ 留言功能（可靠传输）
- ✅ 错误检测和恢复
- ✅ 功耗优化
- ✅ 安全隔离

协议简单高效，适合ESP32-S3的资源限制，同时保证了通讯的可靠性。

