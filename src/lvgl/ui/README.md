# UI 控件层

## 职责

实现自定义控件的纯 UI，不关心业务逻辑。

## 子目录

- `base/` - 基础控件（容器、画布）
- `controls/` - 可操作控件（按钮、滑动条、开关）
- `indicators/` - 指示控件（标签、进度条、LED）
- `containers/` - 布局容器（列表、网格、选项卡）

## 回调接口规范

```c
/* 基础回调 - 所有控件都有 */
typedef void (*ui_create_cb_t)(lv_obj_t* obj, void* user_data);
typedef void (*ui_destroy_cb_t)(lv_obj_t* obj, void* user_data);

/* 操作回调 - 可操作控件额外提供 */
typedef void (*ui_action_cb_t)(lv_obj_t* obj, int event_code, void* user_data);

/* 值变化回调 - 值类控件提供 */
typedef void (*ui_value_cb_t)(lv_obj_t* obj, int value, void* user_data);
```

## 控件命名规范

- 文件: `ui_<类型>.c/h` (如 `ui_card.c`, `ui_slider.c`)
- 创建函数: `lv_obj_t* ui_<类型>_create(lv_obj_t* parent, const ui_<类型>_config_t* config)`
- 销毁函数: `void ui_<类型>_destroy(lv_obj_t* obj)`
