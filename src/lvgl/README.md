# LVGL UI 模块架构

## 六层架构设计

```
┌─────────────────────────────────────────────────────────────┐
│                        pages (页面层)                         │
│   组合 ui 控件和 widgets 窗口，形成完整的用户界面页面              │
├─────────────────────────────────────────────────────────────┤
│                      widgets (窗口层)                         │
│   由 ui 控件组成的最小可操作界面（对话框、输入框、键盘等）          │
├─────────────────────────────────────────────────────────────┤
│                         ui (控件层)                           │
│   自定义控件的纯 UI 实现，只提供回调接口，不关心业务逻辑           │
├─────────────────────────────────────────────────────────────┤
│                      navigator (导航层)                       │
│   管理页面注册、父子关系、页面栈、当前显示页面                     │
├─────────────────────────────────────────────────────────────┤
│               presenters (展示器层) + bridge (桥接层)          │
│   presenters: 实现具体页面/窗口/控件的功能和回调                  │
│   bridge: 对接后端程序和硬件驱动，实现业务与 UI 解耦              │
└─────────────────────────────────────────────────────────────┘
```

## 目录结构

```
src/lvgl/
├── ui/                          # 控件层 - 纯 UI 实现
│   ├── base/                    #   基础控件（容器、画布等）
│   ├── controls/                #   可操作控件（按钮、滑动条、开关等）
│   ├── indicators/              #   指示控件（标签、进度条、LED等）
│   └── containers/              #   布局容器（列表、网格、选项卡等）
│
├── widgets/                     # 窗口层 - 最小可操作界面
│   ├── dialog/                  #   对话框（警告、确认、消息）
│   ├── input/                   #   输入窗口（文本、数字、WiFi配置）
│   ├── keyboard/                #   键盘窗口（虚拟键盘）
│   └── panel/                   #   面板窗口（状态面板、设置面板）
│
├── pages/                       # 页面层 - 完整界面
│   ├── home/                    #   主页
│   ├── settings/                #   设置页
│   ├── lora/                    #   LoRa 通讯页
│   ├── audio/                   #   音频页
│   └── about/                   #   关于页
│
├── navigator/                   # 导航层 - 页面管理
│   ├── nav_manager.c/h          #     导航管理器
│   ├── nav_stack.c/h            #     页面栈
│   └── nav_indicator.c/h        #     导航指示器
│
├── bridge/                      # 桥接层 - 业务对接
│   ├── bridge_wifi.c/h          #     WiFi 业务桥接
│   ├── bridge_lora.c/h          #     LoRa 业务桥接
│   ├── bridge_audio.c/h         #     音频业务桥接
│   ├── bridge_system.c/h        #     系统信息桥接
│   └── bridge_storage.c/h       #     存储业务桥接
│
├── presenters/                  # 展示器层 - 功能实现
│   ├── pages/                   #     页面展示器
│   │   ├── presenter_home.c/h
│   │   ├── presenter_settings.c/h
│   │   ├── presenter_lora.c/h
│   │   └── presenter_audio.c/h
│   ├── widgets/                 #     窗口展示器
│   │   ├── presenter_dialog.c/h
│   │   ├── presenter_wifi_input.c/h
│   │   └── presenter_keyboard.c/h
│   └── controls/                #     控件展示器
│       ├── presenter_button.c/h
│       └── presenter_slider.c/h
│
├── themes/                      # 主题
├── fonts/                       # 字体
├── assets/                      # 资源文件
├── lvgl_app.c/h                 # 应用入口
└── lv_conf.h                    # LVGL 配置
```

## 各层职责

### 1. ui (控件层)

**职责**: 实现自定义控件的纯视觉和交互 UI

**原则**:
- 只关心控件长什么样、怎么响应用户操作
- 不关心控件被用在哪里、实现什么业务
- 对外提供统一格式的回调函数接口
- 相同类型控件的回调函数签名一致

**回调接口规范**:
```c
// 普通控件：创建和销毁回调
typedef void (*ui_create_cb)(lv_obj_t* obj, void* user_data);
typedef void (*ui_destroy_cb)(lv_obj_t* obj, void* user_data);

// 可操作控件：额外提供操作回调
typedef void (*ui_action_cb)(lv_obj_t* obj, lv_event_t* event, void* user_data);
```

### 2. widgets (窗口层)

**职责**: 组合 ui 控件，形成用户直接操作的最小界面单元

**示例**:
- 警告对话框: 标题 + 消息 + 确认按钮
- WiFi 配置窗口: SSID 输入框 + 密码输入框 + 连接按钮
- 键盘窗口: 虚拟键盘 + 输入预览

### 3. pages (页面层)

**职责**: 组合 ui 控件和 widgets 窗口，形成完整的功能页面

**示例**:
- 主页: 状态栏 + 快捷按钮 + 信息卡片
- 设置页: 设置列表 + 各种配置窗口
- LoRa 页: 消息列表 + 输入框 + 发送按钮

### 4. navigator (导航层)

**职责**: 管理页面的生命周期和导航

**功能**:
- 页面注册/注销
- 页面栈管理（前进/后退）
- 父子页面关系
- 页面切换动画
- 当前页面状态维护

### 5. bridge (桥接层)

**职责**: 屏蔽后端实现，为 presenters 提供统一的数据接口

**原则**:
- UI 层不直接调用后端/硬件
- 通过 bridge 获取数据和执行操作
- 便于单元测试和模拟

### 6. presenters (展示器层)

**职责**: 实现具体的业务逻辑，连接 UI 和后端

**职责**:
- 实现 ui 控件的回调函数
- 通过 bridge 获取数据
- 更新 UI 显示
- 处理用户交互

## 数据流

```
用户操作 → ui 控件触发回调 → presenter 处理逻辑
                ↓
        presenter 通过 bridge 调用后端
                ↓
        bridge 返回结果给 presenter
                ↓
        presenter 更新 ui 控件显示
```

## 添加新功能的流程

1. **ui 层**: 如需新控件，在 `ui/` 下添加
2. **widgets 层**: 如需新窗口，在 `widgets/` 下添加
3. **pages 层**: 如需新页面，在 `pages/` 下添加
4. **bridge 层**: 如需对接新后端，在 `bridge/` 下添加
5. **presenters 层**: 实现具体的回调和业务逻辑
6. **navigator**: 注册新页面
