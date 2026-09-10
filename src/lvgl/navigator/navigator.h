/**
 * @file navigator.h
 * @brief 导航层：页面注册表 + push/pop/switch + 生命周期 + 待机接口（契约 I6）
 *
 * 页面通过 navigator_page_t 注册（id 唯一），栈管理纯逻辑在 nav_stack
 * （可 PC 单测）。on_enter/on_exit 在页面显示/隐藏时回调（页面在
 * on_enter 拉数据、on_exit 停自身 timer）。
 *
 * 待机（I6）：navigator_set_standby(cb, timeout_s) 注入待机画面创建函数，
 * 无输入超时后覆盖显示；任意触摸输入恢复。cb=NULL 关闭待机。
 */

#ifndef NAVIGATOR_H
#define NAVIGATOR_H

#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
typedef enum {
    NAV_OK = 0,
    NAV_ERR_PARAM = -1,
    NAV_ERR_NOT_FOUND = -2,    /* id 未注册 */
    NAV_ERR_FULL = -3,         /* 注册表/页面栈满 */
    NAV_ERR_NO_PAGE = -4,      /* 栈空无可返回页 */
    NAV_ERR_STATE = -5,        /* 状态不允许（如重复注册） */
} lvgl_nav_err_t;

/** 页面创建回调：在 parent 上构建整页 UI，返回页面根对象 */
typedef lv_obj_t* (*screen_create_cb)(lv_obj_t* parent);

/** 页面描述符（须静态存储期） */
typedef struct {
    const char* id;             /* 唯一 id，如 "home" */
    screen_create_cb create;    /* 页面构建函数 */
    void (*on_enter)(void);     /* 可选：显示后回调 */
    void (*on_exit)(void);      /* 可选：隐藏前回调 */
} navigator_page_t;

#define NAVIGATOR_MAX_PAGES 8

/* ========== 公共 API ========== */

/** 初始化导航器（幂等） */
void navigator_init(lv_obj_t* root);

/** 注册页面（id 重复返回 NAV_ERR_STATE） */
lvgl_nav_err_t navigator_register(const navigator_page_t* page);

/** 压栈显示（新页覆盖当前页，当前页 on_exit 但对象保留） */
lvgl_nav_err_t navigator_push(const char* id);

/** 弹栈返回上一页（栈底不可弹出） */
lvgl_nav_err_t navigator_pop(void);

/** 平级替换栈顶（home↔settings 切换，不加深栈） */
lvgl_nav_err_t navigator_switch(const char* id);

/** 当前页面 id（无页面返回 NULL） */
const char* navigator_current(void);

/** 重建当前页（销毁+重进，语言热切换用） */
lvgl_nav_err_t navigator_reload(void);

/**
 * @brief 契约 I6：设置待机画面
 * @param cb 待机画面创建函数（NULL=关闭待机）
 * @param timeout_s 无输入超时（秒；cb 非 0 时生效）
 * @return NAV_OK / NAV_ERR_PARAM
 */
lvgl_nav_err_t navigator_set_standby(screen_create_cb cb, uint32_t timeout_s);

/** 待机覆盖是否激活 */
bool navigator_standby_active(void);

#ifdef __cplusplus
}
#endif

#endif /* NAVIGATOR_H */
