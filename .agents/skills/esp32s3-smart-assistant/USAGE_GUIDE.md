# ESP32-S3 智能助手开发技能使用指南

## 快速开始

### 1. 技能已创建完成

技能位置：`/home/olwhistle/dockerNow/esp32/programs/ovs/.agents/skills/esp32s3-smart-assistant/`

### 2. 如何使用技能

当你需要开发ESP32-S3智能助手项目时，直接告诉我：

```
我要开发ESP32-S3智能助手项目
```

或者：

```
帮我开发LoRa通讯模块
```

技能会自动：
1. 读取开发日志
2. 了解当前进度
3. 制定开发任务
4. 遵循模块化规范开发

## 技能功能

### 1. 代码模块化开发

**特点**：
- 严格的模块划分
- 清晰的接口设计
- 标准化的代码结构
- 自动遵循规范

**示例**：
```
帮我开发音频管理模块
```

技能会：
- 创建 `audio_manager/` 目录
- 生成标准的头文件和源文件
- 遵循命名规范
- 实现标准接口

### 2. 日志管理系统

**特点**：
- 自动记录开发进度
- 追踪问题和解决方案
- 每次启动自动读取
- 恢复开发上下文

**日志位置**：`docs/development_log.md`（项目根目录下）

**日志内容**：
```markdown
## [日期时间] - [任务标题]

### 完成内容
- [x] 任务1
- [x] 任务2

### 待解决问题
- 问题1：描述
- 问题2：描述

### 解决方案
- 问题1：解决方案

### 下一步计划
- 计划1
- 计划2

### 代码变更
- 修改文件：xxx.c
- 新增文件：xxx.h
```

### 3. 开发流程

**启动流程**：
```
读取日志 → 了解状态 → 制定任务
```

**开发流程**：
```
创建代码 → 遵循规范 → 测试验证 → 记录问题
```

**结束流程**：
```
记录进度 → 更新日志 → 制定计划
```

## 使用场景

### 场景1：开发新模块

**用户**：帮我开发LoRa通讯模块

**技能**：
1. 读取日志了解当前状态
2. 创建 `lora_manager/` 目录和文件
3. 遵循模块化规范编写代码
4. 实现标准接口（init, start, stop等）
5. 测试功能
6. 更新日志记录进度

### 场景2：修复Bug

**用户**：LoRa模块无法初始化

**技能**：
1. 读取日志查看相关记录
2. 分析问题原因
3. 修复代码
4. 测试验证
5. 更新日志记录解决方案

### 场景3：查看进度

**用户**：查看开发进度

**技能**：
1. 读取日志文件
2. 显示最近的开发记录
3. 列出待解决问题
4. 显示下一步计划

### 场景4：功能优化

**用户**：优化音频播放延迟

**技能**：
1. 读取日志了解当前实现
2. 分析瓶颈
3. 实现优化方案
4. 测试性能
5. 更新日志记录优化结果

## 代码规范

### 命名规范
```c
// 函数
lora_send_data()
audio_play_file()
display_show_message()

// 变量
lora_buffer
audio_volume
display_brightness

// 类型
lora_msg_t
audio_config_t
display_state_t

// 宏
LORA_MAX_MSG_SIZE
AUDIO_SAMPLE_RATE
DISPLAY_WIDTH
```

### 模块接口
每个模块提供标准接口：
```c
esp_err_t module_init(const config_t *config);
esp_err_t module_deinit(void);
esp_err_t module_start(void);
esp_err_t module_stop(void);
esp_err_t module_get_status(status_t *status);
esp_err_t module_set_callback(callback_t callback);
```

### 代码结构
```c
// 头文件结构
#ifndef MODULE_H
#define MODULE_H

// 包含文件
// 类型定义
// 函数声明

#endif

// 源文件结构
#include "module.h"

// 私有变量
// 私有函数声明
// 公共函数实现
// 私有函数实现
```

## 日志系统详解

### 1. 日志读取时机
- 每次启动技能时自动读读取
- 了解当前进度状态
- 恢复开发上下文

### 2. 日志更新时机
- 每次开发结束时更新
- 记录完成内容
- 记录遇到的问题
- 记录解决方案
- 制定下一步计划

### 3. 日志格式示例
```markdown
## 2026-09-03 15:30 - LoRa模块开发

### 完成内容
- [x] 创建lora_manager目录
- [x] 实现lora_manager.h头文件
- [x] 实现lora_manager.c源文件
- [x] 实现基本初始化功能

### 进行中
- [ ] 实现LoRa发送功能
- [ ] 实现LoRa接收功能

### 待解决问题
- 问题1：LoRa初始化失败
  - 原因：SPI引脚配置错误
  - 解决方案：检查原理图，修正引脚配置

### 下一步计划
1. 实现LoRa发送功能
2. 实现LoRa接收功能
3. 测试通讯距离

### 代码变更
- 新增文件：firmware/main/components/lora_manager/lora_manager.h
- 新增文件：firmware/main/components/lora_manager/lora_manager.c
- 新增文件：firmware/main/components/lora_manager/CMakeLists.txt
```

## 模板使用

### 1. 头文件模板
位置：`templates/module_template.h`

使用方法：
1. 复制模板
2. 替换 `[module_name]` 为实际模块名
3. 添加模块特定的接口

### 2. 源文件模板
位置：`templates/module_template.c`

使用方法：
1. 复制模板
2. 替换 `[module_name]` 为实际模块名
3. 实现模块功能

### 3. CMake模板
位置：`templates/CMakeLists_template.txt`

使用方法：
1. 复制模板
2. 替换 `[module_name]` 为实际模块名
3. 添加依赖项

## 参考文档

### 1. 项目结构规范
位置：`references/project_structure.md`

内容：
- 目录结构规范
- 模块划分原则
- 文件命名规范
- 依赖关系图

### 2. 代码规范
位置：`references/coding_standards.md`

内容：
- 命名规范
- 代码格式
- 注释规范
- 错误处理
- 内存管理
- 任务管理

## 常见问题

### Q1：如何开始开发？
**A**：直接告诉我"我要开发ESP32-S3智能助手项目"，技能会自动读取日志并开始开发。

### Q2：如何查看进度？
**A**：告诉我"查看开发进度日志"，技能会读取日志并显示当前状态。

### Q3：如何修复Bug？
**A**：描述问题现象，技能会分析问题、修复代码并更新日志。

### Q4：如何优化功能？
**A**：描述优化需求，技能会分析瓶颈、实现优化并更新日志。

### Q5：日志在哪里？
**A**：`docs/development_log.md`（项目根目录下）

## 注意事项

1. **日志必须**：每次开发必须记录日志
2. **模块化**：严格遵循模块划分规范
3. **接口清晰**：模块间通过标准接口通讯
4. **代码质量**：清晰的命名和注释
5. **错误处理**：完善的错误处理机制
6. **测试验证**：每个功能都要测试验证

## 版本信息

- **版本**: 1.0.0
- **创建日期**: 2026-09-03
- **适用项目**: ESP32-S3桌面智能助手
- **技能位置**: `/home/olwhistle/dockerNow/esp32/programs/ovs/.agents/skills/esp32s3-smart-assistant/`

## 获取帮助

如需帮助，可以：
1. 查看本使用指南
2. 查看参考文档
3. 直接向技能提问
