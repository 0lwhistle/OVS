/**
 * @file nav_stack.h
 * @brief 导航页面栈（纯逻辑，无 LVGL 依赖，可 PC 单测）
 */

#ifndef NAV_STACK_H
#define NAV_STACK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NAV_STACK_MAX      8     /* 页面栈深度限制 */
#define NAV_STACK_ID_MAX   24    /* 页面 id 最大长度（含 \0） */

typedef struct {
    int  top;                        /* 栈内元素数（0=空） */
    char ids[NAV_STACK_MAX][NAV_STACK_ID_MAX];
} nav_stack_t;

void        nav_stack_reset(nav_stack_t* s);
bool        nav_stack_push(nav_stack_t* s, const char* id);       /* false=满/参数错 */
bool        nav_stack_pop(nav_stack_t* s, char* out, size_t cap); /* false=空；out=弹出 id */
const char* nav_stack_top(const nav_stack_t* s);                  /* 空=NULL */
int         nav_stack_count(const nav_stack_t* s);
bool        nav_stack_replace_top(nav_stack_t* s, const char* id);/* switch 语义 */
bool        nav_stack_contains(const nav_stack_t* s, const char* id);

#ifdef __cplusplus
}
#endif

#endif /* NAV_STACK_H */
