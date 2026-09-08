# AI行为规则

## 重要规则

### 1. 长篇内容输出规则
**规则**：长篇内容（超过500字）直接输出成md文档，不要在终端显示。

**原因**：节省token，提高效率。

**执行方式**：
1. 创建md文档到 `docs/` 目录
2. 告诉用户文档路径
3. 让用户自己查看文档

**示例**：
```
已创建文档：docs/pcb_design_tutorial.md
请查看文档了解详细内容。
```

### 2. 文档命名规范
- 使用英文小写
- 使用下划线分隔单词
- 例如：`pcb_design_tutorial.md`

### 3. 文档存放位置
- 统一存放在 `docs/` 目录
- 按功能分类：
  - 硬件相关：`hardware_*.md`
  - 软件相关：`software_*.md`
  - 教程类：`*_tutorial.md`
  - 注意事项：`*_notes.md`

### 4. 文档更新规则
- 每次更新都要记录到开发日志
- 文档末尾添加"最后更新"时间
- 保持文档结构清晰

---

## 当前项目文档目录

```
docs/
├── hardware_notes_for_software.md    # 硬件注意事项（必读）
├── skill_hardware_requirements.md    # Skill硬件注意事项补充
├── ai_behavior_rules.md              # AI行为规则（本文件）
├── development_roadmap.md            # 开发路线图
├── lora_protocol.md                  # LoRa协议
└── web_interface.md                  # Web界面
```

---

## 软件开发必读文档

每次开发软件前必须阅读：
1. `docs/hardware_notes_for_software.md` - 硬件注意事项
2. `docs/skill_hardware_requirements.md` - Skill硬件注意事项补充

---

**最后更新**：2026-09-03
**维护者**：AI助手
**版本**：1.0
