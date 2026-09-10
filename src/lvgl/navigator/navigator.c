/**
 * @file navigator.c
 * @brief 导航层实现：注册表 + 页面栈 + 生命周期 + 待机覆盖（I6）
 *
 * 页面对象策略：push 不销毁被覆盖页对象（保留状态、切回快），
 * reload 销毁重建（语言热切换）。待机=覆盖层（lv_layer_top() 之上），
 * 底层页面保持存活，唤醒即删覆盖层。
 */

#include "navigator.h"
#include "nav_stack.h"
#include "logger.h"

#include <string.h>

static const char* TAG = "[NAV]";

static const navigator_page_t* s_pages[NAVIGATOR_MAX_PAGES];
static int s_page_count = 0;
static nav_stack_t s_stack;
static lv_obj_t* s_root = NULL;
static lv_obj_t* s_page_objs[NAVIGATOR_MAX_PAGES];   /* 栈内页面根对象（与 s_stack 对齐） */

/* ---- 待机状态（I6） ---- */
static screen_create_cb s_standby_cb = NULL;
static uint32_t s_standby_timeout_s = 0;
static bool s_standby_active = false;
static lv_obj_t* s_standby_obj = NULL;
static lv_timer_t* s_standby_timer = NULL;

static const navigator_page_t* find_page(const char* id) {
    for (int i = 0; i < s_page_count; i++) {
        if (s_pages[i]->id && strcmp(s_pages[i]->id, id) == 0) {
            return s_pages[i];
        }
    }
    return NULL;
}

static lv_obj_t* page_obj_at(int idx) {
    return (idx >= 0 && idx < NAV_STACK_MAX) ? s_page_objs[idx] : NULL;
}

/* ==================== 待机（I6） ==================== */

static void standby_exit(void);

static void standby_wake_cb(lv_event_t* e) {
    (void)e;
    if (s_standby_active) {
        standby_exit();
    }
}

static void standby_timer_cb(lv_timer_t* timer) {
    (void)timer;
    if (s_standby_active || !s_standby_cb || !s_root) {
        return;
    }
    if (lv_display_get_inactive_time(NULL) < (uint32_t)s_standby_timeout_s * 1000u) {
        return;
    }

    LOGI(TAG, "enter standby (idle %us)", (unsigned)(lv_display_get_inactive_time(NULL) / 1000u));
    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_90, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* screen = s_standby_cb(overlay);
    (void)screen;

    lv_obj_add_event_cb(overlay, standby_wake_cb, LV_EVENT_ALL, NULL);
    lv_obj_move_foreground(overlay);
    s_standby_obj = overlay;
    s_standby_active = true;
}

static void standby_exit(void) {
    if (!s_standby_active) {
        return;
    }
    if (s_standby_obj) {
        lv_obj_delete(s_standby_obj);
        s_standby_obj = NULL;
    }
    s_standby_active = false;
    lv_display_trigger_activity(NULL);
    LOGI(TAG, "standby exited");
}

lvgl_nav_err_t navigator_set_standby(screen_create_cb cb, uint32_t timeout_s) {
    if (!s_root) {
        return NAV_ERR_STATE;
    }
    if (cb && timeout_s == 0) {
        return NAV_ERR_PARAM;
    }
    s_standby_cb = cb;
    s_standby_timeout_s = timeout_s;

    if (cb && !s_standby_timer) {
        s_standby_timer = lv_timer_create(standby_timer_cb, 1000, NULL);
    } else if (!cb && s_standby_timer) {
        lv_timer_del(s_standby_timer);
        s_standby_timer = NULL;
        standby_exit();
    }
    LOGI(TAG, "standby %s (timeout=%us)", cb ? "enabled" : "disabled", (unsigned)timeout_s);
    return NAV_OK;
}

bool navigator_standby_active(void) {
    return s_standby_active;
}

/* ==================== 页面栈 ==================== */

void navigator_init(lv_obj_t* root) {
    if (s_root) {
        LOGW(TAG, "Already initialized");
        return;
    }
    s_root = root;
    s_page_count = 0;
    memset(s_page_objs, 0, sizeof(s_page_objs));
    nav_stack_reset(&s_stack);
    LOGI(TAG, "navigator ready");
}

lvgl_nav_err_t navigator_register(const navigator_page_t* page) {
    if (!page || !page->id || !page->create) {
        return NAV_ERR_PARAM;
    }
    if (find_page(page->id)) {
        LOGW(TAG, "page '%s' already registered", page->id);
        return NAV_ERR_STATE;
    }
    if (s_page_count >= NAVIGATOR_MAX_PAGES) {
        return NAV_ERR_FULL;
    }
    s_pages[s_page_count++] = page;
    LOGI(TAG, "page registered: %s", page->id);
    return NAV_OK;
}

/** 构建 id 指定页面为根对象并压栈显示（栈管理公共路径） */
static lvgl_nav_err_t show_page(const char* id) {
    const navigator_page_t* page = find_page(id);
    if (!page) {
        LOGW(TAG, "page '%s' not registered", id ? id : "?");
        return NAV_ERR_NOT_FOUND;
    }
    int idx = nav_stack_count(&s_stack);
    if (idx >= NAV_STACK_MAX) {
        LOGW(TAG, "page stack full");
        return NAV_ERR_FULL;
    }

    lv_obj_t* obj = page->create(s_root);
    if (!obj) {
        LOGE(TAG, "page '%s' create failed", page->id);
        return NAV_ERR_STATE;
    }
    lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(obj, lv_pct(100), lv_pct(100));
    lv_obj_move_foreground(obj);

    if (!nav_stack_push(&s_stack, id)) {
        lv_obj_delete(obj);
        return NAV_ERR_FULL;
    }
    s_page_objs[idx] = obj;

    if (page->on_enter) {
        page->on_enter();
    }
    return NAV_OK;
}

lvgl_nav_err_t navigator_push(const char* id) {
    if (!s_root) {
        return NAV_ERR_STATE;
    }
    if (s_standby_active) {
        standby_exit();
    }
    /* 隐藏当前页 */
    int top_idx = nav_stack_count(&s_stack) - 1;
    lv_obj_t* cur = page_obj_at(top_idx);
    if (cur) {
        const navigator_page_t* cur_page = find_page(nav_stack_top(&s_stack));
        if (cur_page && cur_page->on_exit) {
            cur_page->on_exit();
        }
        lv_obj_add_flag(cur, LV_OBJ_FLAG_HIDDEN);
    }
    return show_page(id);
}

lvgl_nav_err_t navigator_pop(void) {
    if (!s_root || nav_stack_count(&s_stack) <= 1) {
        return NAV_ERR_NO_PAGE;
    }
    if (s_standby_active) {
        standby_exit();
    }

    /* 销毁栈顶 */
    int top_idx = nav_stack_count(&s_stack) - 1;
    const navigator_page_t* top_page = find_page(nav_stack_top(&s_stack));
    if (top_page && top_page->on_exit) {
        top_page->on_exit();
    }
    lv_obj_t* top_obj = page_obj_at(top_idx);
    if (top_obj) {
        lv_obj_delete(top_obj);
    }
    s_page_objs[top_idx] = NULL;
    nav_stack_pop(&s_stack, NULL, 0);

    /* 恢复新栈顶 */
    const navigator_page_t* page = find_page(nav_stack_top(&s_stack));
    lv_obj_t* below = page_obj_at(nav_stack_count(&s_stack) - 1);
    if (page && below) {
        lv_obj_clear_flag(below, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(below);
        if (page->on_enter) {
            page->on_enter();
        }
    }
    return NAV_OK;
}

lvgl_nav_err_t navigator_switch(const char* id) {
    if (!s_root) {
        return NAV_ERR_STATE;
    }
    if (nav_stack_count(&s_stack) == 0) {
        return show_page(id);   /* 空栈等价 push */
    }
    if (s_standby_active) {
        standby_exit();
    }

    /* 弹出栈顶（id 值拷贝，pop 会原地清空）→ 销毁对象 → 压入新页 */
    int top_idx = nav_stack_count(&s_stack) - 1;
    char cur_id[NAV_STACK_ID_MAX];
    nav_stack_pop(&s_stack, cur_id, sizeof(cur_id));

    const navigator_page_t* top_page = find_page(cur_id);
    if (top_page && top_page->on_exit) {
        top_page->on_exit();
    }
    lv_obj_t* top_obj = page_obj_at(top_idx);
    if (top_obj) {
        lv_obj_delete(top_obj);
    }
    s_page_objs[top_idx] = NULL;
    return show_page(id);
}

const char* navigator_current(void) {
    return nav_stack_top(&s_stack);
}

lvgl_nav_err_t navigator_reload(void) {
    if (!s_root || nav_stack_count(&s_stack) == 0) {
        return NAV_ERR_NO_PAGE;
    }
    if (s_standby_active) {
        standby_exit();
    }
    /* nav_stack_top 返回栈内数组指针，pop 会原地清空，须值拷贝 */
    char cur_id[NAV_STACK_ID_MAX];
    if (!nav_stack_pop(&s_stack, cur_id, sizeof(cur_id))) {
        return NAV_ERR_NO_PAGE;
    }
    const navigator_page_t* page = find_page(cur_id);
    if (!page) {
        return NAV_ERR_NOT_FOUND;
    }

    int top_idx = nav_stack_count(&s_stack);
    if (page->on_exit) {
        page->on_exit();
    }
    lv_obj_t* top_obj = page_obj_at(top_idx);
    if (top_obj) {
        lv_obj_delete(top_obj);
    }
    s_page_objs[top_idx] = NULL;
    return show_page(cur_id);
}
