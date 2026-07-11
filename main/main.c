#include "task_manager.h"
#include "task_worker.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "[MAIN_TEST]";

/* ==================== 测试辅助结构 ==================== */
struct test_ctx {
    int id;
    int count;
    int expected_count;
    int timeout_flag;
};

/* ==================== 测试任务函数 ==================== */

// 1. 基础任务：累加计数
enum task_t task_count(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    tc->count++;
    printf("[task_count_%d] count = %d\n", tc->id, tc->count);
    return TASK_OK;
}

// 2. 返回失败的任务
enum task_t task_fail(void* ctx) {
    printf("[task_fail] I will fail\n");
    return TASK_FUNC_ERR;
}

// 3. 超时任务：模拟长时间运行
enum task_t task_timeout(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    printf("[task_timeout_%d] start, will sleep 200ms\n", tc->id);
    usleep(200 * 1000);  // 200ms
    printf("[task_timeout_%d] done\n", tc->id);
    return TASK_OK;
}

// 4. 慢任务（lots 级别）
enum task_t task_slow(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    tc->count++;
    printf("[task_slow_%d] count = %d (lots level)\n", tc->id, tc->count);
    return TASK_OK;
}

// 5. 周期任务
enum task_t task_periodic(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    tc->count++;
    printf("[task_periodic] tick %d\n", tc->count);
    return TASK_OK;
}

// 6. 取消测试任务
enum task_t task_to_cancel(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    tc->count++;
    printf("[task_to_cancel] count = %d (should never run after cancel)\n", tc->count);
    return TASK_OK;
}

// 7. 优先级测试任务
enum task_t task_pri(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    printf("[task_pri_%d] running\n", tc->id);
    return TASK_OK;
}

// 8. 级别升级测试任务
enum task_t task_level_up(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    tc->count++;
    printf("[task_level_up] count = %d\n", tc->count);
    return TASK_OK;
}

/* ==================== 测试用例 ==================== */

// 测试1: 基础即时任务（little 级别，执行10次）
void test_basic_immediate(void) {
    printf("\n========== Test 1: Basic Immediate Task ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 1;
    tc->count = 0;

    struct task_node* node = sched_task_init_li(0, 10, "test_immediate", task_count, tc);
    if (node) {
        shched_enqueue(node);
    }
}

// 测试2: 延迟调度任务（period=100ms, 执行5次）
void test_delayed_schedule(void) {
    printf("\n========== Test 2: Delayed Schedule Task ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 2;
    tc->count = 0;

    struct task_node* node = sched_task_init_mi(100, 5, "test_delayed", task_count, tc);
    if (node) {
        shched_enqueue(node);
    }
}

// 测试3: 无限周期任务（period=500ms, run_cnt=-1）
void test_periodic_task(void) {
    printf("\n========== Test 3: Periodic Task ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 3;
    tc->count = 0;

    struct task_node* node = sched_task_init_mi(500, -1, "test_periodic", task_periodic, tc);
    if (node) {
        shched_enqueue(node);
    }
}

// 测试4: 任务失败处理
void test_task_fail(void) {
    printf("\n========== Test 4: Task Fail Handling ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 4;
    tc->count = 0;

    struct task_node* node = sched_task_init_li(0, 1, "test_fail", task_fail, tc);
    if (node) {
        shched_enqueue(node);
    }
}

// 测试5: 超时检测（little 默认超时50ms，任务跑200ms）
void test_timeout_detection(void) {
    printf("\n========== Test 5: Timeout Detection ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 5;
    tc->count = 0;

    struct task_node* node = sched_task_init_li(0, 1, "test_timeout", task_timeout, tc);
    if (node) {
        shched_enqueue(node);
    }
}

// 测试6: lots 级别慢任务
void test_lots_level(void) {
    printf("\n========== Test 6: Lots Level Task ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 6;
    tc->count = 0;

    struct task_node* node = sched_task_init_lo(5000, 0, 3, "test_lots", task_slow, tc);
    if (node) {
        shched_enqueue(node);
    }
}

// 测试7: 取消任务
void test_cancel_task(void) {
    printf("\n========== Test 7: Cancel Task ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 7;
    tc->count = 0;

    struct task_node* node = sched_task_init_li(0, 10, "test_cancel", task_to_cancel, tc);
    if (node) {
        shched_enqueue(node);
        // 立即取消
        shched_cancel_by_name("test_cancel");
        printf("[test_cancel] task cancelled\n");
    }
}

// 测试8: 多个任务同时调度
void test_multi_task(void) {
    printf("\n========== Test 8: Multiple Tasks ==========\n");
    for (int i = 0; i < 5; i++) {
        struct test_ctx* tc = malloc(sizeof(struct test_ctx));
        tc->id = 100 + i;
        tc->count = 0;

        char name[32];
        snprintf(name, sizeof(name), "test_multi_%d", i);
        // 需要持久化 name，用 strdup
        char* name_dup = strdup(name);

        struct task_node* node = sched_task_init_li(0, 1, name_dup, task_pri, tc);
        if (node) {
            shched_enqueue(node);
        }
    }
}

// 测试9: 调度表满的情况（SCHED_TASK_QUEUE_SIZE=32）
void test_sched_full(void) {
    printf("\n========== Test 9: Sched Table Full ==========\n");
    int count = 0;
    for (int i = 0; i < 40; i++) {
        struct test_ctx* tc = malloc(sizeof(struct test_ctx));
        tc->id = 200 + i;
        tc->count = 0;

        char name[32];
        snprintf(name, sizeof(name), "test_full_%d", i);
        char* name_dup = strdup(name);

        struct task_node* node = sched_task_init_li(0, 1, name_dup, task_pri, tc);
        if (node) {
            int ret = shched_enqueue(node);
            if (ret == TASK_QUEUE_FULL) {
                printf("[test_sched_full] queue full at %d\n", i);
                free(tc);
                free(name_dup);
                break;
            }
            count++;
        }
    }
    printf("[test_sched_full] enqueued %d tasks\n", count);
}

// 测试10: 级别升级（timeout 后 little→middle）
void test_level_upgrade(void) {
    printf("\n========== Test 10: Level Upgrade ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 10;
    tc->count = 0;

    // little 级别，默认超时50ms，任务跑200ms，会超时触发级别升级
    struct task_node* node = sched_task_init_li(0, 3, "test_level_up", task_timeout, tc);
    if (node) {
        shched_enqueue(node);
    }
}

// 测试11: 取消不存在的任务（边界测试）
void test_cancel_nonexist(void) {
    printf("\n========== Test 11: Cancel Non-existent Task ==========\n");
    shched_cancel_by_name("nonexistent_task");
    printf("[test_cancel_nonexist] cancel non-existent task, no crash\n");
}

// 测试12: 空参数测试
void test_null_param(void) {
    printf("\n========== Test 12: Null Parameter ==========\n");
    int ret = shched_enqueue(NULL);
    printf("[test_null_param] shched_enqueue(NULL) = %d\n", ret);
}

// 测试13: 检查调度表状态
void test_check_status(void) {
    printf("\n========== Test 13: Check Status ==========\n");
    printf("[test_check_status] is_full = %d, is_empty = %d\n", 
           shched_is_full(), shched_is_empty());
}

// 测试14: 周期任务取消
void test_cancel_periodic(void) {
    printf("\n========== Test 14: Cancel Periodic Task ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 14;
    tc->count = 0;

    struct task_node* node = sched_task_init_mi(200, -1, "test_cancel_periodic", task_periodic, tc);
    if (node) {
        shched_enqueue(node);
        // 等1秒后取消
        usleep(1000 * 1000);
        shched_cancel_by_name("test_cancel_periodic");
        printf("[test_cancel_periodic] periodic task cancelled after 1s, ran %d times\n", tc->count);
    }
}

// 测试15: 不同优先级任务混合
void test_priority_mix(void) {
    printf("\n========== Test 15: Priority Mix ==========\n");
    // 先提交一个 last 优先级的
    struct test_ctx* tc1 = malloc(sizeof(struct test_ctx));
    tc1->id = 301;
    tc1->count = 0;
    struct task_node* node1 = sched_task_init_li(0, 1, "test_pri_low", task_pri, tc1);
    if (node1) {
        node1->pri = last;
        shched_enqueue(node1);
    }

    // 再提交一个 first 优先级的
    struct test_ctx* tc2 = malloc(sizeof(struct test_ctx));
    tc2->id = 302;
    tc2->count = 0;
    struct task_node* node2 = sched_task_init_li(0, 1, "test_pri_high", task_pri, tc2);
    if (node2) {
        node2->pri = first;
        shched_enqueue(node2);
    }
}

void app_main(void)
{
    printf("\n========================================\n");
    printf("  TASKER FULL TEST SUITE START\n");
    printf("========================================\n");

    // 初始化 worker 系统
    int ret = worker_init();
    if (ret != 0) {
        printf("worker_init failed: %d\n", ret);
        return;
    }

    // 按顺序执行测试用例
    test_null_param();          // 1. 空参数
    test_cancel_nonexist();     // 2. 取消不存在任务
    test_check_status();        // 3. 初始状态检查
    test_basic_immediate();     // 4. 基础即时任务
    test_task_fail();           // 5. 任务失败
    test_cancel_task();         // 6. 取消任务
    test_multi_task();          // 7. 多任务
    test_priority_mix();        // 8. 优先级混合
    test_delayed_schedule();    // 9. 延迟调度
    test_timeout_detection();   // 10. 超时检测
    test_level_upgrade();       // 11. 级别升级
    test_lots_level();          // 12. lots 级别
    test_periodic_task();       // 13. 周期任务
    test_cancel_periodic();     // 14. 取消周期任务
    test_sched_full();          // 15. 调度表满

    printf("\n========================================\n");
    printf("  ALL TESTS SUBMITTED\n");
    printf("  (check serial output for results)\n");
    printf("========================================\n");
}
