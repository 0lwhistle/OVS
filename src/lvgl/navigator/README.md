# Navigator 导航层

## 职责

管理页面的生命周期、导航关系和显示状态。

## 核心功能

1. **页面注册**: 注册可用页面及其创建/销毁函数
2. **页面栈**: 管理页面的前进/后退历史
3. **父子关系**: 管理页面的层级关系
4. **页面切换**: 控制页面切换动画和时机
5. **状态维护**: 维护当前页面状态

## 接口设计

```c
/* 页面注册 */
typedef struct {
    page_id_t id;
    const char* name;
    page_create_fn create;
    page_destroy_fn destroy;
} page_descriptor_t;

/* 导航管理器 API */
void nav_init(lv_obj_t* screen);
void nav_register_page(const page_descriptor_t* desc);
void nav_push_page(page_id_t id);
void nav_pop_page(void);
void nav_switch_page(page_id_t id);
page_id_t nav_get_current_page(void);
void nav_set_parent(page_id_t child, page_id_t parent);
```

## 页面栈示意

```
[主页] → [设置] → [WiFi设置]
                  ↑
            [LoRa设置]

当前栈: [主页, 设置, WiFi设置]
返回: pop → [主页, 设置]
