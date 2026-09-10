/**
 * @file test_nav_stack.c
 * @brief navigator 页面栈纯逻辑单测（[GUI] 批次②，PC 门禁）
 *
 * 覆盖：reset/push/pop/top/replace_top/contains、深度上限、非法参数、
 * switch 语义（替换栈顶不加深）、空栈操作。
 */

#include "nav_stack.h"

#include <stdio.h>
#include <string.h>

static int s_pass = 0, s_fail = 0;
#define CHECK(cond) do { \
    if (cond) { s_pass++; } \
    else { s_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void test_basic_push_pop(void) {
    printf("[NS1] push/pop/top basics\n");
    nav_stack_t s;
    nav_stack_reset(&s);

    CHECK(nav_stack_count(&s) == 0);
    CHECK(nav_stack_top(&s) == NULL);
    CHECK(!nav_stack_pop(&s, NULL, 0));          /* 空栈 pop 失败 */

    CHECK(nav_stack_push(&s, "home"));
    CHECK(nav_stack_count(&s) == 1);
    CHECK(strcmp(nav_stack_top(&s), "home") == 0);

    CHECK(nav_stack_push(&s, "settings"));
    CHECK(nav_stack_count(&s) == 2);
    CHECK(strcmp(nav_stack_top(&s), "settings") == 0);

    char popped[NAV_STACK_ID_MAX];
    CHECK(nav_stack_pop(&s, popped, sizeof(popped)));
    CHECK(strcmp(popped, "settings") == 0);
    CHECK(strcmp(nav_stack_top(&s), "home") == 0);

    CHECK(nav_stack_pop(&s, NULL, 0));
    CHECK(nav_stack_count(&s) == 0);
}

static void test_depth_limit(void) {
    printf("[NS2] depth limit (%d)\n", NAV_STACK_MAX);
    nav_stack_t s;
    nav_stack_reset(&s);

    char id[8];
    for (int i = 0; i < NAV_STACK_MAX; i++) {
        snprintf(id, sizeof(id), "p%d", i);
        CHECK(nav_stack_push(&s, id));
    }
    CHECK(nav_stack_count(&s) == NAV_STACK_MAX);
    CHECK(!nav_stack_push(&s, "overflow"));      /* 满栈 push 失败 */
    CHECK(nav_stack_count(&s) == NAV_STACK_MAX);

    /* 弹到空 */
    for (int i = 0; i < NAV_STACK_MAX; i++) {
        CHECK(nav_stack_pop(&s, NULL, 0));
    }
    CHECK(nav_stack_count(&s) == 0);
    CHECK(!nav_stack_pop(&s, NULL, 0));
}

static void test_invalid_params(void) {
    printf("[NS3] invalid params\n");
    nav_stack_t s;
    nav_stack_reset(&s);

    CHECK(!nav_stack_push(&s, NULL));            /* NULL id */
    CHECK(!nav_stack_push(&s, ""));              /* 空 id */
    CHECK(!nav_stack_push(&s, "this_id_is_way_too_long_for_stack"));  /* 超长 */
    CHECK(nav_stack_count(&s) == 0);
    CHECK(nav_stack_push(&s, "ok"));
    CHECK(nav_stack_count(&s) == 1);
}

static void test_switch_semantics(void) {
    printf("[NS4] switch (replace top, no depth growth)\n");
    nav_stack_t s;
    nav_stack_reset(&s);

    nav_stack_push(&s, "home");
    CHECK(nav_stack_replace_top(&s, "settings"));
    CHECK(nav_stack_count(&s) == 1);
    CHECK(strcmp(nav_stack_top(&s), "settings") == 0);

    /* 空栈 replace 失败 */
    nav_stack_t empty;
    nav_stack_reset(&empty);
    CHECK(!nav_stack_replace_top(&empty, "x"));
}

static void test_contains(void) {
    printf("[NS5] contains\n");
    nav_stack_t s;
    nav_stack_reset(&s);

    nav_stack_push(&s, "home");
    nav_stack_push(&s, "settings");
    CHECK(nav_stack_contains(&s, "home"));
    CHECK(nav_stack_contains(&s, "settings"));
    CHECK(!nav_stack_contains(&s, "standby"));
    CHECK(!nav_stack_contains(&s, NULL));
}

void test_nav_stack_run(int* pass, int* fail) {
    printf("\n==== nav_stack tests ====\n");
    test_basic_push_pop();
    test_depth_limit();
    test_invalid_params();
    test_switch_semantics();
    test_contains();
    *pass += s_pass;
    *fail += s_fail;
    printf("==== nav_stack tests done: %d passed, %d failed ====\n", s_pass, s_fail);
}
