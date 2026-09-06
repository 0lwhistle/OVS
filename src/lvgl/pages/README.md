# Pages 页面层

## 职责

组合 ui 控件和 widgets 窗口，形成功能完整的页面。

## 子目录

- `home/` - 主页
- `settings/` - 设置页
- `lora/` - LoRa 通讯页
- `audio/` - 音频播放页
- `about/` - 关于页

## 页面命名规范

- 文件: `page_<名称>.c/h` (如 `page_home.c`)
- 创建函数: `lv_obj_t* page_<名称>_create(lv_obj_t* parent)`
- 销毁函数: `void page_<名称>_destroy(lv_obj_t* obj)`

## 页面结构

每个页面通常包含：
- 状态栏（从 widgets 获取）
- 主内容区域（组合 ui 控件）
- 导航元素（返回按钮、菜单等）

## 页面与导航

页面通过 navigator 注册后，由导航管理器控制显示和切换。
页面本身不关心自己何时显示、如何切换。
