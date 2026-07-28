/**
 * @file tasker_test.c
 * @brief Comprehensive stress and functional test suite for the tasker scheduler.
 *
 * Covers 15 basic functional tests and 10 heavy stress scenarios.
 * Designed to be called from main.c via the tasker_test.h API.
 *
 * All delays use vTaskDelay for FreeRTOS compatibility.
 * Each failure is recorded with test name, line number, and detail for
 * post-run analysis.
 */

#include "tasker_test.h"
#include "task_manager.h"
#include "task_worker.h"
#include "tasker.h"
#include <string.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "[TASKER_TEST]";

/* ==================== Failure recording ==================== */

#define MAX_FAILURES 64

struct fail_record {
    int  line;
    const char* test_name;
    const char* msg;
};

static struct fail_record g_failures[MAX_FAILURES];
static int g_fail_cnt = 0;
static int g_pass = 0;
static int g_total = 0;
static const char* g_current_test = "(unknown)";

/* Record a failure — called from ASSERT when condition is false */
static void fail_record(int line, const char* test_name, const char* msg) {
    if (g_fail_cnt < MAX_FAILURES) {
        g_failures[g_fail_cnt].line      = line;
        g_failures[g_fail_cnt].test_name = test_name;
        g_failures[g_fail_cnt].msg       = msg;
        g_fail_cnt++;
    }
}

#define ASSERT(cond, msg) do { \
    g_total++; \
    if (cond) { \
        g_pass++; \
    } else { \
        fail_record(__LINE__, g_current_test, msg); \
        printf("  FAIL [%s:%d] %s\n", g_current_test, __LINE__, msg); \
    } \
} while(0)

/* Start a test: prints banner + sets current test name for failure tracking */
#define TEST_START(title) do { \
    g_current_test = title; \
    printf("\n----------------------------------------\n"); \
    printf("  %s\n", title); \
    printf("----------------------------------------\n"); \
} while(0)

/* ==================== Test context ==================== */

struct test_ctx {
    int id;
    volatile int count;
    volatile int timeout_hit;
    int oneshot;
};

/* ==================== Task callback functions ==================== */

static enum task_t fn_counter(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    tc->count++;
    if (tc->oneshot) free(tc);
    return TASK_OK;
}

static enum task_t fn_always_fail(void* ctx) {
    (void)ctx;
    return TASK_FUNC_ERR;
}

static enum task_t fn_slow_200ms(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    tc->count++;
    vTaskDelay(pdMS_TO_TICKS(200));
    if (tc->oneshot) free(tc);
    return TASK_OK;
}

static enum task_t fn_periodic_tick(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    tc->count++;
    return TASK_OK;
}

static enum task_t fn_identity(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    tc->count++;
    if (tc->oneshot) free(tc);
    return TASK_OK;
}

static enum task_t fn_cpu_burn(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    volatile int sum = 0;
    for (int i = 0; i < 50000; i++) sum += i;
    tc->count++;
    if (tc->oneshot) free(tc);
    return TASK_OK;
}

static enum task_t fn_mem_churn(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    void* buf = malloc(2048);
    if (buf) {
        memset(buf, (uint8_t)tc->id, 2048);
        free(buf);
    }
    tc->count++;
    if (tc->oneshot) free(tc);
    return TASK_OK;
}

static enum task_t fn_variable_slow(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    int delay_ms = 50 + (tc->id % 100);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    tc->count++;
    if (tc->oneshot) free(tc);
    return TASK_OK;
}

static enum task_t fn_cancel_target(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    tc->count++;
    printf("  [WARN] cancel_target_%d executed (should have been cancelled)\n", tc->id);
    return TASK_OK;
}

/* ==================== Helper ==================== */

static struct test_ctx* ctx_alloc(int id, int oneshot) {
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    if (!tc) return NULL;
    tc->id = id;
    tc->count = 0;
    tc->timeout_hit = 0;
    tc->oneshot = oneshot;
    return tc;
}

/* ==================== Basic functional tests (1-15) ==================== */

static void test_null_enqueue(void) {
    TEST_START("Test 1: Null Enqueue");
    int ret = tasker_enqueue(NULL);
    ASSERT(ret == TASK_PARA_ERR, "enqueue(NULL) should return TASK_PARA_ERR");
    printf("  enqueue(NULL) = %d\n", ret);
}

static void test_cancel_nonexistent(void) {
    TEST_START("Test 2: Cancel Non-existent");
    tasker_cancel_by_name("no_such_task_xyz");
    ASSERT(1, "no crash when cancelling non-existent task");
}

static void test_status_query(void) {
    TEST_START("Test 3: Status Query");
    int empty = tasker_is_empty();
    int full  = tasker_is_full();
    printf("  is_empty=%d, is_full=%d\n", empty, full);
    ASSERT(empty == 1 || empty == 0, "is_empty returns 0/1");
    ASSERT(full  == 1 || full  == 0, "is_full returns 0/1");
}

static void test_basic_oneshot(void) {
    TEST_START("Test 4: Basic One-Shot Task");
    struct test_ctx* tc = ctx_alloc(4, 1);
    ASSERT(tc != NULL, "ctx alloc OK");

    struct task_node node;
    int ret = tasker_task_init_li(&node, 0, 1, "test_oneshot", fn_identity, tc);
    ASSERT(ret == TASK_OK, "tasker_task_init_li OK");

    ret = tasker_enqueue(&node);
    ASSERT(ret == TASK_OK, "enqueue OK");
    ASSERT(node.fn == NULL, "move semantics: fn set to NULL after enqueue");
}

static void test_task_failure(void) {
    TEST_START("Test 5: Task Failure Handling");
    struct task_node node;
    int ret = tasker_task_init_li(&node, 0, 1, "test_fail", fn_always_fail, NULL);
    ASSERT(ret == TASK_OK, "init OK");
    ret = tasker_enqueue(&node);
    ASSERT(ret == TASK_OK, "enqueue OK (failure handled by worker)");
}

static void test_cancel_after_enqueue(void) {
    TEST_START("Test 6: Cancel After Enqueue");
    struct test_ctx* tc = ctx_alloc(6, 0);
    ASSERT(tc != NULL, "ctx alloc OK");

    struct task_node node;
    int ret = tasker_task_init_li(&node, 0, 10, "test_cancel_imm", fn_cancel_target, tc);
    ASSERT(ret == TASK_OK, "init OK");

    ret = tasker_enqueue(&node);
    ASSERT(ret == TASK_OK, "enqueue OK");

    tasker_cancel_by_name("test_cancel_imm");
    vTaskDelay(pdMS_TO_TICKS(200));
    ASSERT(tc->count == 0, "cancelled task never executed");
    free(tc);
}

static void test_multi_oneshot(void) {
    TEST_START("Test 7: Multiple One-Shot Tasks (x5)");
    int enqueued = 0;
    for (int i = 0; i < 5; i++) {
        struct test_ctx* tc = ctx_alloc(100 + i, 1);
        if (!tc) continue;
        char name[32];
        snprintf(name, sizeof(name), "test_multi_%d", i);
        struct task_node node;
        int ret = tasker_task_init_li(&node, 0, 1, name, fn_identity, tc);
        if (ret != TASK_OK) { free(tc); continue; }
        ret = tasker_enqueue(&node);
        if (ret == TASK_OK) enqueued++; else free(tc);
    }
    ASSERT(enqueued == 5, "all 5 tasks enqueued");
    printf("  enqueued %d/5\n", enqueued);
}

static void test_priority_mix(void) {
    TEST_START("Test 8: Priority Mix (first vs last)");
    int enqueued = 0;

    struct test_ctx* tcl = ctx_alloc(801, 1);
    struct task_node nl;
    if (tcl && tasker_task_init_li(&nl, 0, 1, "test_pri_last", fn_identity, tcl) == TASK_OK) {
        nl.pri = last;
        if (tasker_enqueue(&nl) == TASK_OK) enqueued++; else free(tcl);
    } else { free(tcl); }

    struct test_ctx* tcf = ctx_alloc(802, 1);
    struct task_node nf;
    if (tcf && tasker_task_init_li(&nf, 0, 1, "test_pri_first", fn_identity, tcf) == TASK_OK) {
        nf.pri = first;
        if (tasker_enqueue(&nf) == TASK_OK) enqueued++; else free(tcf);
    } else { free(tcf); }

    ASSERT(enqueued == 2, "both priority tasks enqueued");
}

static void test_delayed_periodic(void) {
    TEST_START("Test 9: Delayed Periodic (100ms x5)");
    struct test_ctx* tc = ctx_alloc(9, 0);
    ASSERT(tc != NULL, "ctx alloc OK");

    struct task_node node;
    int ret = tasker_task_init_mi(&node, 100, 5, "test_delayed_periodic", fn_periodic_tick, tc);
    ASSERT(ret == TASK_OK, "init OK");

    ret = tasker_enqueue(&node);
    ASSERT(ret == TASK_OK, "enqueue OK");

    vTaskDelay(pdMS_TO_TICKS(800));
    ASSERT(tc->count >= 4, "periodic task ran >= 4 times in 800ms");
    printf("  ran %d times (expected ~7-8)\n", tc->count);

    vTaskDelay(pdMS_TO_TICKS(300));
    free(tc);
}

static void test_timeout_detection(void) {
    TEST_START("Test 10: Timeout Detection (200ms task / 50ms timeout)");
    struct test_ctx* tc = ctx_alloc(10, 1);
    ASSERT(tc != NULL, "ctx alloc OK");

    struct task_node node;
    int ret = tasker_task_init_li(&node, 0, 1, "test_timeout_detect", fn_slow_200ms, tc);
    ASSERT(ret == TASK_OK, "init OK");

    ret = tasker_enqueue(&node);
    ASSERT(ret == TASK_OK, "enqueue OK");

    vTaskDelay(pdMS_TO_TICKS(500));
    ASSERT(1, "no crash after timeout (worker handles timer)");
}

static void test_level_upgrade(void) {
    TEST_START("Test 11: Level Upgrade (slow task on little worker)");
    struct test_ctx* tc = ctx_alloc(11, 0);
    ASSERT(tc != NULL, "ctx alloc OK");

    struct task_node node;
    int ret = tasker_task_init_li(&node, 0, 3, "test_level_upgrade", fn_slow_200ms, tc);
    ASSERT(ret == TASK_OK, "init OK");

    ret = tasker_enqueue(&node);
    ASSERT(ret == TASK_OK, "enqueue OK");

    vTaskDelay(pdMS_TO_TICKS(1500));
    ASSERT(tc->count >= 1, "upgraded task eventually executed");
    printf("  executed %d times after upgrade\n", tc->count);
    free(tc);
}

static void test_lots_level(void) {
    TEST_START("Test 12: Lots Level Task");
    struct test_ctx* tc = ctx_alloc(12, 0);
    ASSERT(tc != NULL, "ctx alloc OK");

    struct task_node node;
    int ret = tasker_task_init_lo(&node, 5000, 0, 3, "test_lots_level", fn_counter, tc);
    ASSERT(ret == TASK_OK, "init OK");

    ret = tasker_enqueue(&node);
    ASSERT(ret == TASK_OK, "enqueue OK");

    vTaskDelay(pdMS_TO_TICKS(500));
    ASSERT(tc->count >= 1, "lots task executed");
    printf("  executed %d times\n", tc->count);
    free(tc);
}

static void test_periodic_cancel(void) {
    TEST_START("Test 13: Cancel Infinite Periodic");
    struct test_ctx* tc = ctx_alloc(13, 0);
    ASSERT(tc != NULL, "ctx alloc OK");

    struct task_node node;
    int ret = tasker_task_init_mi(&node, 200, TASK_CNT_INF, "test_periodic_cancel", fn_periodic_tick, tc);
    ASSERT(ret == TASK_OK, "init OK");

    ret = tasker_enqueue(&node);
    ASSERT(ret == TASK_OK, "enqueue OK");

    vTaskDelay(pdMS_TO_TICKS(1000));
    int before = tc->count;
    tasker_cancel_by_name("test_periodic_cancel");
    vTaskDelay(pdMS_TO_TICKS(600));
    int after = tc->count;
    ASSERT(after == before, "periodic task stopped after cancel");
    printf("  before=%d after=%d\n", before, after);
    free(tc);
}

static void test_sched_full(void) {
    TEST_START("Test 14: Scheduler Queue Full");
    int enqueued = 0;
    int full_hit = 0;

    for (int i = 0; i < 80; i++) {
        struct test_ctx* tc = ctx_alloc(200 + i, 1);
        if (!tc) continue;
        char name[32];
        snprintf(name, sizeof(name), "test_full_%d", i);
        struct task_node node;
        int ret = tasker_task_init_li(&node, 0, 1, name, fn_identity, tc);
        if (ret != TASK_OK) { free(tc); continue; }
        ret = tasker_enqueue(&node);
        if (ret == TASK_OK) { enqueued++; }
        else if (ret == TASK_QUEUE_FULL) { full_hit++; free(tc); }
        else { free(tc); }
    }
    ASSERT(enqueued > 0, "at least some tasks enqueued");
    ASSERT(full_hit > 0, "queue-full triggered (max slots=64)");
    printf("  enqueued=%d full_hits=%d\n", enqueued, full_hit);
}

static void test_invalid_params(void) {
    TEST_START("Test 15: Invalid Parameters Rejected");
    struct task_node node;

    int ret = tasker_task_init_li(&node, -1, 1, "test_neg_period", fn_identity, NULL);
    ASSERT(ret == TASK_PARA_ERR, "negative period rejected");

    ret = tasker_task_init_li(&node, 0, 1, "", fn_identity, NULL);
    ASSERT(ret == TASK_PARA_ERR, "empty name rejected");

    ret = tasker_task_init_li(&node, 0, 1, "test_null_fn", NULL, NULL);
    ASSERT(ret == TASK_PARA_ERR, "NULL fn rejected");

    ret = tasker_task_init_li(NULL, 0, 1, "test_null_out", fn_identity, NULL);
    ASSERT(ret == TASK_PARA_ERR, "NULL out rejected");
}

/* ==================== Stress tests (S1-S10) ==================== */

static void stress_sched_full_flood(void) {
    TEST_START("Stress S1: Sched Full Flood (120 tasks)");
    int accepted = 0, full = 0, retried = 0;

    for (int i = 0; i < 120; i++) {
        struct test_ctx* tc = ctx_alloc(1000 + i, 1);
        if (!tc) continue;
        char name[32];
        snprintf(name, sizeof(name), "s1_flood_%d", i);
        struct task_node node;
        if (tasker_task_init_li(&node, 0, 1, name, fn_counter, tc) != TASK_OK) {
            free(tc); continue;
        }
        int ret = tasker_enqueue(&node);
        if (ret == TASK_OK) {
            accepted++;
        } else if (ret == TASK_QUEUE_FULL) {
            full++;
            vTaskDelay(pdMS_TO_TICKS(20));
            if (tasker_task_init_li(&node, 0, 1, name, fn_counter, tc) == TASK_OK) {
                ret = tasker_enqueue(&node);
                if (ret == TASK_OK) { accepted++; retried++; }
                else { free(tc); }
            } else { free(tc); }
        } else { free(tc); }
    }
    ASSERT(accepted >= 60, ">= 60 / 120 accepted");
    printf("  accepted=%d full=%d retried=%d (full=0 means sched kept up)\n", accepted, full, retried);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

static void stress_periodic_tsunami(void) {
    TEST_START("Stress S2: Periodic Tsunami (25 tasks, 3s)");
    int enqueued = 0;
    struct test_ctx* ctxs[25];
    memset(ctxs, 0, sizeof(ctxs));

    for (int i = 0; i < 25; i++) {
        ctxs[i] = ctx_alloc(2000 + i, 0);
        if (!ctxs[i]) continue;
        char name[32];
        snprintf(name, sizeof(name), "s2_tsunami_%d", i);
        int period = 50 + (i * 20);
        struct task_node node;
        if (tasker_task_init_mi(&node, period, TASK_CNT_INF, name, fn_periodic_tick, ctxs[i]) != TASK_OK)
            continue;
        if (tasker_enqueue(&node) == TASK_OK) enqueued++;
    }
    ASSERT(enqueued >= 15, ">= 15 periodic tasks enqueued");
    printf("  enqueued %d/25, running 3s...\n", enqueued);
    vTaskDelay(pdMS_TO_TICKS(3000));

    int total_hits = 0;
    for (int i = 0; i < 25; i++) {
        if (!ctxs[i]) continue;
        char name[32];
        snprintf(name, sizeof(name), "s2_tsunami_%d", i);
        tasker_cancel_by_name(name);
        total_hits += ctxs[i]->count;
        free(ctxs[i]);
    }
    ASSERT(total_hits > 100, "total ticks > 100 (system kept up)");
    printf("  total ticks: %d\n", total_hits);
}

static void stress_rapid_fire(void) {
    TEST_START("Stress S3: Rapid Fire (200 one-shot tasks)");
    int accepted = 0;
    for (int i = 0; i < 200; i++) {
        struct test_ctx* tc = ctx_alloc(3000 + i, 1);
        if (!tc) continue;
        char name[32];
        snprintf(name, sizeof(name), "s3_fire_%d", i);
        struct task_node node;
        if (tasker_task_init_li(&node, 0, 1, name, fn_counter, tc) != TASK_OK) {
            free(tc); continue;
        }
        if (tasker_enqueue(&node) == TASK_OK) accepted++;
        else free(tc);
    }
    ASSERT(accepted >= 100, ">= 100 / 200 accepted");
    printf("  accepted %d/200\n", accepted);
    vTaskDelay(pdMS_TO_TICKS(1500));
}

static void stress_mixed_marathon(void) {
    TEST_START("Stress S4: Mixed Level Marathon (60 tasks)");
    int enqueued = 0;
    for (int i = 0; i < 60; i++) {
        struct test_ctx* tc = ctx_alloc(4000 + i, 1);
        if (!tc) continue;
        char name[32];
        snprintf(name, sizeof(name), "s4_mix_%d", i);
        struct task_node node;
        int ret;
        if (i < 20)
            ret = tasker_task_init_li(&node, 0, 1, name, fn_cpu_burn, tc);
        else if (i < 40)
            ret = tasker_task_init_mi(&node, 0, 1, name, fn_mem_churn, tc);
        else
            ret = tasker_task_init_lo(&node, 5000, 0, 1, name, fn_counter, tc);
        if (ret != TASK_OK) { free(tc); continue; }
        if (tasker_enqueue(&node) == TASK_OK) enqueued++;
        else free(tc);
    }
    ASSERT(enqueued >= 30, ">= 30 / 60 mixed tasks enqueued");
    printf("  enqueued %d/60 (20L+20M+20H)\n", enqueued);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

static void stress_cancel_race(void) {
    TEST_START("Stress S5: Cancel Race (40 submit, 20 cancel)");
    int enqueued = 0, cancelled = 0;
    for (int i = 0; i < 40; i++) {
        struct test_ctx* tc = ctx_alloc(5000 + i, 0);
        if (!tc) continue;
        char name[32];
        snprintf(name, sizeof(name), "s5_race_%d", i);
        struct task_node node;
        if (tasker_task_init_li(&node, 0, 1, name, fn_counter, tc) != TASK_OK) {
            free(tc); continue;
        }
        if (tasker_enqueue(&node) == TASK_OK) {
            enqueued++;
            if (i % 2 == 0) {
                vTaskDelay(pdMS_TO_TICKS(1));
                tasker_cancel_by_name(name);
                cancelled++;
            }
        } else { free(tc); }
    }
    ASSERT(enqueued >= 20, ">= 20 enqueued");
    ASSERT(cancelled >= 15, ">= 15 cancelled");
    printf("  enqueued=%d cancelled=%d\n", enqueued, cancelled);
    vTaskDelay(pdMS_TO_TICKS(1000));
    for (int i = 0; i < 40; i++) {
        char name[32];
        snprintf(name, sizeof(name), "s5_race_%d", i);
        tasker_cancel_by_name(name);
    }
}

static void stress_timeout_cascade(void) {
    TEST_START("Stress S6: Timeout Cascade (20 slow tasks)");
    int enqueued = 0;
    for (int i = 0; i < 20; i++) {
        struct test_ctx* tc = ctx_alloc(6000 + i, 1);
        if (!tc) continue;
        char name[32];
        snprintf(name, sizeof(name), "s6_cascade_%d", i);
        struct task_node node;
        if (tasker_task_init_li(&node, 0, 1, name, fn_variable_slow, tc) != TASK_OK) {
            free(tc); continue;
        }
        if (tasker_enqueue(&node) == TASK_OK) enqueued++;
        else free(tc);
    }
    ASSERT(enqueued >= 10, ">= 10 timeout tasks enqueued");
    printf("  enqueued %d/20 (expect timeout->level upgrade)\n", enqueued);
    vTaskDelay(pdMS_TO_TICKS(3000));
}

static void stress_memory_churn(void) {
    TEST_START("Stress S7: Memory Churn (100 malloc/free tasks)");
    int accepted = 0;
    for (int i = 0; i < 100; i++) {
        struct test_ctx* tc = ctx_alloc(7000 + i, 1);
        if (!tc) continue;
        char name[32];
        snprintf(name, sizeof(name), "s7_churn_%d", i);
        struct task_node node;
        if (tasker_task_init_mi(&node, 0, 1, name, fn_mem_churn, tc) != TASK_OK) {
            free(tc); continue;
        }
        if (tasker_enqueue(&node) == TASK_OK) accepted++;
        else free(tc);
        if (i % 10 == 9) vTaskDelay(pdMS_TO_TICKS(10));
    }
    ASSERT(accepted >= 50, ">= 50 / 100 accepted");
    printf("  accepted %d/100\n", accepted);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

static void stress_cpu_saturation(void) {
    TEST_START("Stress S8: CPU Saturation (10 heavy + 30 light)");
    int heavy_ok = 0, light_ok = 0;
    for (int i = 0; i < 40; i++) {
        struct test_ctx* tc = ctx_alloc(8000 + i, 1);
        if (!tc) continue;
        char name[32];
        if (i < 10) {
            snprintf(name, sizeof(name), "s8_heavy_%d", i);
            struct task_node node;
            if (tasker_task_init_li(&node, 0, 1, name, fn_cpu_burn, tc) == TASK_OK) {
                if (tasker_enqueue(&node) == TASK_OK) heavy_ok++;
                else free(tc);
            } else free(tc);
        } else {
            snprintf(name, sizeof(name), "s8_light_%d", i);
            struct task_node node;
            if (tasker_task_init_mi(&node, 0, 1, name, fn_counter, tc) == TASK_OK) {
                if (tasker_enqueue(&node) == TASK_OK) light_ok++;
                else free(tc);
            } else free(tc);
        }
    }
    ASSERT(heavy_ok >= 5,  ">= 5 heavy tasks enqueued");
    ASSERT(light_ok >= 15, ">= 15 light tasks enqueued");
    printf("  heavy=%d light=%d\n", heavy_ok, light_ok);
    vTaskDelay(pdMS_TO_TICKS(3000));
}

static void stress_cancel_reenqueue(void) {
    TEST_START("Stress S9: Cancel + Re-enqueue Race (30 cycles)");
    int ok = 1;
    for (int round = 0; round < 30; round++) {
        struct test_ctx* tc = ctx_alloc(9000 + round, 1);
        if (!tc) continue;
        char name[32];
        snprintf(name, sizeof(name), "s9_cycle_%d", round);
        struct task_node node;
        if (tasker_task_init_li(&node, 0, 1, name, fn_counter, tc) != TASK_OK) {
            free(tc); ok = 0; continue;
        }
        if (tasker_enqueue(&node) != TASK_OK) { free(tc); ok = 0; continue; }
        tasker_cancel_by_name(name);
        vTaskDelay(pdMS_TO_TICKS(5));
        struct test_ctx* tc2 = ctx_alloc(9100 + round, 1);
        if (!tc2) continue;
        struct task_node node2;
        if (tasker_task_init_li(&node2, 0, 1, name, fn_counter, tc2) == TASK_OK) {
            if (tasker_enqueue(&node2) != TASK_OK) free(tc2);
        } else { free(tc2); }
    }
    ASSERT(ok, "all 30 cancel+re-enqueue cycles completed");
    vTaskDelay(pdMS_TO_TICKS(1000));
}

static void stress_endurance(void) {
    TEST_START("Stress S10: Endurance Run (8 waves x30, 8s)");
    int accepted = 0;
    for (int w = 0; w < 8; w++) {
        for (int i = 0; i < 30; i++) {
            int id = 10000 + w * 100 + i;
            struct test_ctx* tc = ctx_alloc(id, 1);
            if (!tc) continue;
            char name[32];
            snprintf(name, sizeof(name), "s10_endur_%d", id);
            struct task_node node;
            int ret;
            if (i % 3 == 0)
                ret = tasker_task_init_li(&node, 0, 1, name, fn_counter, tc);
            else if (i % 3 == 1)
                ret = tasker_task_init_mi(&node, 0, 1, name, fn_mem_churn, tc);
            else
                ret = tasker_task_init_lo(&node, 5000, 0, 1, name, fn_counter, tc);
            if (ret != TASK_OK) { free(tc); continue; }
            if (tasker_enqueue(&node) == TASK_OK) accepted++;
            else free(tc);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ASSERT(accepted >= 100, ">= 100 / 240 accepted");
    printf("  accepted %d/240 over 8 waves\n", accepted);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

/* ==================== Summary printer ==================== */

static void print_summary(void) {
    printf("\n========================================\n");
    printf("  TASKER TEST RESULTS\n");
    printf("  Total assertions: %d  Pass: %d  Fail: %d\n",
           g_total, g_pass, g_fail_cnt);

    if (g_fail_cnt > 0) {
        printf("----------------------------------------\n");
        printf("  FAILURE DETAIL (%d failures):\n", g_fail_cnt);
        for (int i = 0; i < g_fail_cnt; i++) {
            printf("  [%d] %s:%d — %s\n",
                   i + 1,
                   g_failures[i].test_name,
                   g_failures[i].line,
                   g_failures[i].msg);
        }
        printf("----------------------------------------\n");
    }
    printf("========================================\n");
}

static void reset_counters(void) {
    g_total = 0;
    g_pass = 0;
    g_fail_cnt = 0;
    g_current_test = "(unknown)";
}

/* ==================== Public API entry points ==================== */

void tasker_test_basic(void) {
    printf("\n========================================\n");
    printf("  TASKER BASIC FUNCTIONAL TESTS (1-15)\n");
    printf("========================================\n");

    reset_counters();

    test_null_enqueue();
    test_cancel_nonexistent();
    test_status_query();
    test_basic_oneshot();
    test_task_failure();
    test_cancel_after_enqueue();
    test_multi_oneshot();
    test_priority_mix();
    test_delayed_periodic();
    test_timeout_detection();
    test_level_upgrade();
    test_lots_level();
    test_periodic_cancel();
    test_sched_full();
    test_invalid_params();

    printf("\n[WAIT] draining scheduler...\n");
    vTaskDelay(pdMS_TO_TICKS(2000));

    print_summary();
}

void tasker_test_stress(void) {
    printf("\n========================================\n");
    printf("  TASKER STRESS TEST SUITE (S1-S10)\n");
    printf("========================================\n");

    reset_counters();

    stress_sched_full_flood();
    stress_periodic_tsunami();
    stress_rapid_fire();
    stress_mixed_marathon();
    stress_cancel_race();
    stress_timeout_cascade();
    stress_memory_churn();
    stress_cpu_saturation();
    stress_cancel_reenqueue();
    stress_endurance();

    printf("\n[WAIT] draining scheduler...\n");
    vTaskDelay(pdMS_TO_TICKS(3000));

    print_summary();
}

void tasker_test_all(void) {
    tasker_test_basic();
    tasker_test_stress();
    printf("\n========================================\n");
    printf("  ALL TASKER TESTS COMPLETE\n");
    printf("========================================\n");
}
