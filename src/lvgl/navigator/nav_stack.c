/**
 * @file nav_stack.c
 * @brief 导航页面栈实现（纯逻辑）
 */

#include "nav_stack.h"

#include <string.h>

void nav_stack_reset(nav_stack_t* s) {
    if (s) {
        s->top = 0;
        memset(s->ids, 0, sizeof(s->ids));
    }
}

bool nav_stack_push(nav_stack_t* s, const char* id) {
    if (!s || !id || !id[0] || strlen(id) >= NAV_STACK_ID_MAX) {
        return false;
    }
    if (s->top >= NAV_STACK_MAX) {
        return false;
    }
    strncpy(s->ids[s->top], id, NAV_STACK_ID_MAX - 1);
    s->ids[s->top][NAV_STACK_ID_MAX - 1] = '\0';
    s->top++;
    return true;
}

bool nav_stack_pop(nav_stack_t* s, char* out, size_t cap) {
    if (!s || s->top <= 0) {
        return false;
    }
    s->top--;
    if (out && cap > 0) {
        strncpy(out, s->ids[s->top], cap - 1);
        out[cap - 1] = '\0';
    }
    s->ids[s->top][0] = '\0';
    return true;
}

const char* nav_stack_top(const nav_stack_t* s) {
    if (!s || s->top <= 0) {
        return NULL;
    }
    return s->ids[s->top - 1];
}

int nav_stack_count(const nav_stack_t* s) {
    return s ? s->top : 0;
}

bool nav_stack_replace_top(nav_stack_t* s, const char* id) {
    if (!s || !id || !id[0] || strlen(id) >= NAV_STACK_ID_MAX || s->top <= 0) {
        return false;
    }
    s->top--;
    return nav_stack_push(s, id);
}

bool nav_stack_contains(const nav_stack_t* s, const char* id) {
    if (!s || !id) {
        return false;
    }
    for (int i = 0; i < s->top; i++) {
        if (strcmp(s->ids[i], id) == 0) {
            return true;
        }
    }
    return false;
}
