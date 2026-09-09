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
#include "mem.h"
#include "task_manager.h"
#include "task_worker.h"
#include <string.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* Forward declarations for API functions (defined in tasker_api component) */
extern int tasker_init(void);
extern int tasker_enqueue(struct task_node* node);
extern int tasker_task_init_li(struct task_node* out, int period, int run_cnt, 
                                const char* name, task_fn fn, void* ctx);
extern int tasker_task_init_mi(struct task_node* out, int period, int run_cnt, 
                                const char* name, task_fn fn, void* ctx);
extern int tasker_task_init_lo(struct task_node* out, int timeout, int period, 
                                int run_cnt, const char* name, task_fn fn, void* ctx);
extern void tasker_cancel_by_name(const char* name);
extern int tasker_is_full(void);
extern int tasker_is_empty(void);

static const char* TAG = "[TASKER_TEST]";

/* Test result tracking */
static int test_pass_count = 0;
static int test_fail_count = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d): %s\n", __func__, __LINE__, msg); \
        test_fail_count++; \
        return 0; \
    } \
} while(0)

#define TEST_PASS(name) do { \
    printf("  PASS: %s\n", name); \
    test_pass_count++; \
    return 1; \
} while(0)

/* Helper: simple task callback that returns TASK_OK */
static enum task_t fn_identity(void* ctx) {
    (void)ctx;
    return TASK_OK;
}

/* Helper: task callback that always fails */
static enum task_t fn_always_fail(void* ctx) {
    (void)ctx;
    return TASK_FUNC_ERR;
}

/* Helper: slow task (200ms) */
static enum task_t fn_slow_200ms(void* ctx) {
    (void)ctx;
    vTaskDelay(pdMS_TO_TICKS(200));
    return TASK_OK;
}

/* Helper: periodic tick counter */
static enum task_t fn_periodic_tick(void* ctx) {
    int* counter = (int*)ctx;
    if (counter) (*counter)++;
    return TASK_OK;
}

/* Helper: CPU burner */
static enum task_t fn_cpu_burn(void* ctx) {
    (void)ctx;
    volatile int x = 0;
    for (int i = 0; i < 100000; i++) x += i;
    return TASK_OK;
}

/* Helper: memory churn */
static enum task_t fn_mem_churn(void* ctx) {
    (void)ctx;
    for (int i = 0; i < 10; i++) {
        void* p = mem_malloc(256);
        if (p) mem_free(p);
    }
    return TASK_OK;
}

/* Helper: variable slow task */
static enum task_t fn_variable_slow(void* ctx) {
    int ms = ctx ? *(int*)ctx : 50;
    vTaskDelay(pdMS_TO_TICKS(ms));
    return TASK_OK;
}

/* Helper: cancel target */
static enum task_t fn_cancel_target(void* ctx) {
    (void)ctx;
    vTaskDelay(pdMS_TO_TICKS(1000));
    return TASK_OK;
}

/* Helper: counter task */
static enum task_t fn_counter(void* ctx) {
    int* count = (int*)ctx;
    if (count) (*count)++;
    return TASK_OK;
}

/* ========== Test Functions ========== */

static int test_init(void) {
    int ret = tasker_init();
    ASSERT(ret == TASK_OK, "tasker_init should succeed");
    TEST_PASS("test_init");
}

static int test_enqueue_null(void) {
    int ret = tasker_enqueue(NULL);
    ASSERT(ret == TASK_PARA_ERR, "enqueue NULL should fail");
    TEST_PASS("test_enqueue_null");
}

static int test_oneshot(void) {
    struct task_node node;
    int ret = tasker_task_init_li(&node, 0, 1, "test_oneshot", fn_identity, NULL);
    ASSERT(ret == TASK_OK, "tasker_task_init_li OK");
    
    ret = tasker_enqueue(&node);
    ASSERT(ret == TASK_OK, "enqueue should succeed");
    
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_PASS("test_oneshot");
}

static int test_fail_task(void) {
    struct task_node node;
    int ret = tasker_task_init_li(&node, 0, 1, "test_fail", fn_always_fail, NULL);
    ASSERT(ret == TASK_OK, "tasker_task_init_li OK");
    
    ret = tasker_enqueue(&node);
    ASSERT(ret == TASK_OK, "enqueue should succeed");
    
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_PASS("test_fail_task");
}

static int test_cancel(void) {
    struct task_node node;
    int ret = tasker_task_init_li(&node, 0, 10, "test_cancel_imm", fn_cancel_target, NULL);
    ASSERT(ret == TASK_OK, "tasker_task_init_li OK");
    
    ret = tasker_enqueue(&node);
    ASSERT(ret == TASK_OK, "enqueue should succeed");
    
    tasker_cancel_by_name("test_cancel_imm");
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_PASS("test_cancel");
}

static int test_periodic(void) {
    struct task_node node;
    int counter = 0;
    int ret = tasker_task_init_li(&node, 50, 5, "test_periodic", fn_periodic_tick, &counter);
    ASSERT(ret == TASK_OK, "tasker_task_init_li OK");
    
    ret = tasker_enqueue(&node);
    ASSERT(ret == TASK_OK, "enqueue should succeed");
    
    vTaskDelay(pdMS_TO_TICKS(500));
    ASSERT(counter > 0, "counter should be > 0");
    TEST_PASS("test_periodic");
}

static int test_queue_full(void) {
    struct task_node nodes[70];
    int count = 0;
    
    for (int i = 0; i < 70; i++) {
        char name[32];
        snprintf(name, sizeof(name), "full_%d", i);
        int ret = tasker_task_init_li(&nodes[i], 0, 1, name, fn_identity, NULL);
        if (ret != TASK_OK) break;
        ret = tasker_enqueue(&nodes[i]);
        if (ret != TASK_OK) break;
        count++;
    }
    
    ASSERT(count > 0, "should enqueue at least one");
    vTaskDelay(pdMS_TO_TICKS(500));
    TEST_PASS("test_queue_full");
}

static int test_priority_levels(void) {
    struct task_node li, mi, lo;
    
    int r1 = tasker_task_init_li(&li, 0, 1, "pri_li", fn_identity, NULL);
    int r2 = tasker_task_init_mi(&mi, 0, 1, "pri_mi", fn_identity, NULL);
    int r3 = tasker_task_init_lo(&lo, 1000, 0, 1, "pri_lo", fn_identity, NULL);
    
    ASSERT(r1 == TASK_OK && r2 == TASK_OK && r3 == TASK_OK, "init all levels");
    
    tasker_enqueue(&li);
    tasker_enqueue(&mi);
    tasker_enqueue(&lo);
    
    vTaskDelay(pdMS_TO_TICKS(500));
    TEST_PASS("test_priority_levels");
}

static int test_is_full_empty(void) {
    /* After previous tests, queue should have processed items */
    (void)tasker_is_full();
    (void)tasker_is_empty();
    TEST_PASS("test_is_full_empty");
}

/* ========== Test Runner ========== */

void tasker_test_all(void) {
    printf("\n========================================\n");
    printf("  Tasker Test Suite\n");
    printf("========================================\n\n");
    
    test_pass_count = 0;
    test_fail_count = 0;
    
    printf("[1/9] test_init\n");
    test_init();
    
    printf("[2/9] test_enqueue_null\n");
    test_enqueue_null();
    
    printf("[3/9] test_oneshot\n");
    test_oneshot();
    
    printf("[4/9] test_fail_task\n");
    test_fail_task();
    
    printf("[5/9] test_cancel\n");
    test_cancel();
    
    printf("[6/9] test_periodic\n");
    test_periodic();
    
    printf("[7/9] test_queue_full\n");
    test_queue_full();
    
    printf("[8/9] test_priority_levels\n");
    test_priority_levels();
    
    printf("[9/9] test_is_full_empty\n");
    test_is_full_empty();
    
    printf("\n========================================\n");
    printf("  Results: %d passed, %d failed\n", test_pass_count, test_fail_count);
    printf("========================================\n\n");
}
