# Presenters 展示器层

## 职责

实现具体的业务逻辑，连接 UI 和后端。

## 设计原则

- 实现 ui 控件的回调函数
- 通过 bridge 获取数据
- 更新 ui 控件显示
- 处理用户交互逻辑

## 子目录

- `pages/` - 页面展示器
- `widgets/` - 窗口展示器
- `controls/` - 控件展示器

## 展示器命名规范

- 文件: `presenter_<目标>.c/h`
- 初始化: `void presenter_<目标>_init(void)`
- 更新: `void presenter_<目标>_update(void)`

## 示例: 主页展示器

```c
// presenters/pages/presenter_home.c

#include "bridge_system.h"
#include "bridge_wifi.h"
#include "bridge_lora.h"

/* UI 控件引用（由 page_home 创建后传入） */
static lv_obj_t* s_wifi_label;
static lv_obj_t* s_lora_label;
static lv_obj_t* s_time_label;

/* 回调实现 - 由 ui 控件触发 */
static void on_refresh_btn_clicked(lv_event_t* e) {
    presenter_home_update();
}

/* 数据更新 - 通过 bridge 获取数据并更新 UI */
void presenter_home_update(void) {
    bridge_wifi_status_t wifi_status;
    bridge_wifi_get_status(&wifi_status);
    
    if (wifi_status.connected) {
        lv_label_set_text(s_wifi_label, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(s_wifi_label, lv_color_hex(0x51cf66), 0);
    } else {
        lv_label_set_text(s_wifi_label, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(s_wifi_label, lv_color_hex(0xff6b6b), 0);
    }
    
    // ... 更新其他控件
}

/* 初始化 */
void presenter_home_init(lv_obj_t* wifi_label, lv_obj_t* lora_label, lv_obj_t* time_label) {
    s_wifi_label = wifi_label;
    s_lora_label = lora_label;
    s_time_label = time_label;
    
    presenter_home_update();
}
```

## 数据流

```
[用户点击刷新按钮]
        ↓
[ui 按钮触发回调]
        ↓
[presenter_home 处理逻辑]
        ↓
[bridge_wifi_get_status() 获取数据]
        ↓
[presenter 更新 ui 标签显示]
```
