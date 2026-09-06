# Widgets 窗口层

## 职责

由 ui 控件组成的最小可操作界面，是用户直接接触的界面单元。

## 子目录

- `dialog/` - 对话框（警告、确认、消息提示）
- `input/` - 输入窗口（文本输入、WiFi配置、账号密码）
- `keyboard/` - 键盘窗口（虚拟键盘）
- `panel/` - 面板窗口（状态面板、设置面板）

## 窗口命名规范

- 文件: `widget_<类型>.c/h` (如 `widget_dialog.c`, `widget_wifi_input.c`)
- 创建函数: `lv_obj_t* widget_<类型>_create(lv_obj_t* parent, const widget_<类型>_config_t* config)`
- 销毁函数: `void widget_<类型>_destroy(lv_obj_t* obj)`

## 窗口示例

| 窗口 | 说明 | 组成 |
|------|------|------|
| widget_dialog_alert | 警告对话框 | 标题 + 消息 + 确认按钮 |
| widget_dialog_confirm | 确认对话框 | 标题 + 消息 + 确认/取消按钮 |
| widget_input_text | 文本输入 | 标签 + 输入框 + 确认按钮 |
| widget_input_wifi | WiFi配置 | SSID输入 + 密码输入 + 连接按钮 |
| widget_keyboard | 虚拟键盘 | 键盘控件 + 输入预览 |
| widget_panel_status | 状态面板 | WiFi状态 + LoRa状态 + 电量 |
