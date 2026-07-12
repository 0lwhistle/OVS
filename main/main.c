#include "tasker.h"
#include "web.h"
#include "wifi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "nvs_flash.h"

static const char* TAG = "[MAIN_TEST]";

// Web 服务器任务函数
static void web_server_task(void *arg) {
    // 1. 初始化 SPIFFS 并解压 web 资源
    if (web_spiffs_init() != 0) {
        ESP_LOGE(TAG, "SPIFFS init failed, web server will not start");
        vTaskDelete(NULL);
        return;
    }

    // 2. 启动 Web 服务器（内部轮询循环，不会返回）
    web_server_start();

    vTaskDelete(NULL);
}

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

// 9. 计算密集型任务
enum task_t task_cpu(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    volatile int sum = 0;
    for (int i = 0; i < 100000; i++) sum += i;
    tc->count++;
    printf("[task_cpu_%d] count = %d, sum = %d\n", tc->id, tc->count, sum);
    return TASK_OK;
}

// 10. 内存操作任务
enum task_t task_mem(void* ctx) {
    struct test_ctx* tc = (struct test_ctx*)ctx;
    int* buf = malloc(1024);
    if (buf) {
        memset(buf, tc->id, 1024);
        free(buf);
    }
    tc->count++;
    printf("[task_mem_%d] count = %d\n", tc->id, tc->count);
    return TASK_OK;
}

/* ==================== 测试用例 ==================== */

// 测试1: 基础即时任务（little 级别，执行10次）
void test_basic_immediate(void) {
    printf("\n========== Test 1: Basic Immediate Task ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 1;
    tc->count = 0;

    struct task_node* node = tasker_task_init_li(0, 10, "test_immediate", task_count, tc);
    if (node) {
        tasker_enqueue(node);
    }
}

// 测试2: 延迟调度任务（period=100ms, 执行5次）
void test_delayed_schedule(void) {
    printf("\n========== Test 2: Delayed Schedule Task ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 2;
    tc->count = 0;

    struct task_node* node = tasker_task_init_mi(100, 5, "test_delayed", task_count, tc);
    if (node) {
        tasker_enqueue(node);
    }
}

// 测试3: 无限周期任务（period=500ms, run_cnt=-1）
void test_periodic_task(void) {
    printf("\n========== Test 3: Periodic Task ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 3;
    tc->count = 0;

    struct task_node* node = tasker_task_init_mi(500, -1, "test_periodic", task_periodic, tc);
    if (node) {
        tasker_enqueue(node);
    }
}

// 测试4: 任务失败处理
void test_task_fail(void) {
    printf("\n========== Test 4: Task Fail Handling ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 4;
    tc->count = 0;

    struct task_node* node = tasker_task_init_li(0, 1, "test_fail", task_fail, tc);
    if (node) {
        tasker_enqueue(node);
    }
}

// 测试5: 超时检测（little 默认超时50ms，任务跑200ms）
void test_timeout_detection(void) {
    printf("\n========== Test 5: Timeout Detection ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 5;
    tc->count = 0;

    struct task_node* node = tasker_task_init_li(0, 1, "test_timeout", task_timeout, tc);
    if (node) {
        tasker_enqueue(node);
    }
}

// 测试6: lots 级别慢任务
void test_lots_level(void) {
    printf("\n========== Test 6: Lots Level Task ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 6;
    tc->count = 0;

    struct task_node* node = tasker_task_init_lo(5000, 0, 3, "test_lots", task_slow, tc);
    if (node) {
        tasker_enqueue(node);
    }
}

// 测试7: 取消任务
void test_cancel_task(void) {
    printf("\n========== Test 7: Cancel Task ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 7;
    tc->count = 0;

    struct task_node* node = tasker_task_init_li(0, 10, "test_cancel", task_to_cancel, tc);
    if (node) {
        tasker_enqueue(node);
        // 立即取消
        tasker_cancel_by_name("test_cancel");
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

        struct task_node* node = tasker_task_init_li(0, 1, name_dup, task_pri, tc);
        if (node) {
            int ret = tasker_enqueue(node);
            if (ret != TASK_OK) {
                free(node);
                free(tc);
                free(name_dup);
            }
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

        struct task_node* node = tasker_task_init_li(0, 1, name_dup, task_pri, tc);
        if (node) {
            int ret = tasker_enqueue(node);
            if (ret == TASK_QUEUE_FULL) {
                printf("[test_sched_full] queue full at %d\n", i);
                free(node);
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
    struct task_node* node = tasker_task_init_li(0, 3, "test_level_up", task_timeout, tc);
    if (node) {
        tasker_enqueue(node);
    }
}

// 测试11: 取消不存在的任务（边界测试）
void test_cancel_nonexist(void) {
    printf("\n========== Test 11: Cancel Non-existent Task ==========\n");
    tasker_cancel_by_name("nonexistent_task");
    printf("[test_cancel_nonexist] cancel non-existent task, no crash\n");
}

// 测试12: 空参数测试
void test_null_param(void) {
    printf("\n========== Test 12: Null Parameter ==========\n");
    int ret = tasker_enqueue(NULL);
    printf("[test_null_param] tasker_enqueue(NULL) = %d\n", ret);
}

// 测试13: 检查调度表状态
void test_check_status(void) {
    printf("\n========== Test 13: Check Status ==========\n");
    printf("[test_check_status] is_full = %d, is_empty = %d\n", 
           tasker_is_full(), tasker_is_empty());
}

// 测试14: 周期任务取消
void test_cancel_periodic(void) {
    printf("\n========== Test 14: Cancel Periodic Task ==========\n");
    struct test_ctx* tc = malloc(sizeof(struct test_ctx));
    tc->id = 14;
    tc->count = 0;

    struct task_node* node = tasker_task_init_mi(200, -1, "test_cancel_periodic", task_periodic, tc);
    if (node) {
        tasker_enqueue(node);
        // 等1秒后取消
        usleep(1000 * 1000);
        tasker_cancel_by_name("test_cancel_periodic");
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
    struct task_node* node1 = tasker_task_init_li(0, 1, "test_pri_low", task_pri, tc1);
    if (node1) {
        node1->pri = last;
        tasker_enqueue(node1);
    }

    // 再提交一个 first 优先级的
    struct test_ctx* tc2 = malloc(sizeof(struct test_ctx));
    tc2->id = 302;
    tc2->count = 0;
    struct task_node* node2 = tasker_task_init_li(0, 1, "test_pri_high", task_pri, tc2);
    if (node2) {
        node2->pri = first;
        tasker_enqueue(node2);
    }
}

// ==================== 压力测试 ====================

// 压力测试1: 大量即时任务（填满所有 worker）
void stress_bulk_immediate(void) {
    printf("\n========== Stress 1: Bulk Immediate Tasks (50 tasks) ==========\n");
    int success = 0;
    for (int i = 0; i < 50; i++) {
        struct test_ctx* tc = malloc(sizeof(struct test_ctx));
        tc->id = 1000 + i;
        tc->count = 0;

        char name[32];
        snprintf(name, sizeof(name), "stress_bulk_%d", i);
        char* name_dup = strdup(name);

        struct task_node* node = tasker_task_init_li(0, 1, name_dup, task_count, tc);
        if (node) {
            int ret = tasker_enqueue(node);
            if (ret == TASK_OK) success++;
            else {
                free(node);
                free(tc);
                free(name_dup);
            }
        }
    }
    printf("[stress_bulk] enqueued %d/50 tasks\n", success);
}

// 压力测试2: 混合级别任务
void stress_mixed_levels(void) {
    printf("\n========== Stress 2: Mixed Levels (30 tasks) ==========\n");
    int success = 0;
    for (int i = 0; i < 30; i++) {
        struct test_ctx* tc = malloc(sizeof(struct test_ctx));
        tc->id = 2000 + i;
        tc->count = 0;

        char name[32];
        snprintf(name, sizeof(name), "stress_mix_%d", i);
        char* name_dup = strdup(name);

        struct task_node* node;
        if (i < 10) {
            // little
            node = tasker_task_init_li(0, 1, name_dup, task_cpu, tc);
        } else if (i < 20) {
            // middle
            node = tasker_task_init_mi(0, 1, name_dup, task_mem, tc);
        } else {
            // lots
            node = tasker_task_init_lo(5000, 0, 1, name_dup, task_slow, tc);
        }

        if (node) {
            int ret = tasker_enqueue(node);
            if (ret == TASK_OK) success++;
            else {
                free(node);
                free(tc);
                free(name_dup);
            }
        }
    }
    printf("[stress_mixed] enqueued %d/30 tasks\n", success);
}

// 压力测试3: 快速连续提交（模拟突发流量）
void stress_burst_submit(void) {
    printf("\n========== Stress 3: Burst Submit (20 tasks in quick succession) ==========\n");
    int success = 0;
    for (int burst = 0; burst < 3; burst++) {
        for (int i = 0; i < 20; i++) {
            struct test_ctx* tc = malloc(sizeof(struct test_ctx));
            tc->id = 3000 + burst * 100 + i;
            tc->count = 0;

            char name[32];
            snprintf(name, sizeof(name), "stress_burst_%d_%d", burst, i);
            char* name_dup = strdup(name);

            struct task_node* node = tasker_task_init_li(0, 1, name_dup, task_count, tc);
            if (node) {
                int ret = tasker_enqueue(node);
                if (ret == TASK_OK) success++;
                else {
                    free(node);
                    free(tc);
                    free(name_dup);
                }
            }
        }
        usleep(100 * 1000);  // 每波间隔100ms
    }
    printf("[stress_burst] enqueued %d/60 tasks\n", success);
}

// 压力测试4: 周期任务风暴
void stress_periodic_storm(void) {
    printf("\n========== Stress 4: Periodic Task Storm (10 periodic tasks) ==========\n");
    int success = 0;
    for (int i = 0; i < 10; i++) {
        struct test_ctx* tc = malloc(sizeof(struct test_ctx));
        tc->id = 4000 + i;
        tc->count = 0;

        char name[32];
        snprintf(name, sizeof(name), "stress_periodic_%d", i);
        char* name_dup = strdup(name);

        // 不同周期：100ms ~ 1000ms
        int period = 100 + i * 100;
        struct task_node* node = tasker_task_init_mi(period, -1, name_dup, task_periodic, tc);
        if (node) {
            int ret = tasker_enqueue(node);
            if (ret == TASK_OK) success++;
            else {
                free(node);
                free(tc);
                free(name_dup);
            }
        }
    }
    printf("[stress_periodic] enqueued %d/10 periodic tasks\n", success);
}

// 压力测试5: 取消风暴（提交后立即取消）
void stress_cancel_storm(void) {
    printf("\n========== Stress 5: Cancel Storm (20 tasks, cancel after submit) ==========\n");
    int success = 0;
    for (int i = 0; i < 20; i++) {
        struct test_ctx* tc = malloc(sizeof(struct test_ctx));
        tc->id = 5000 + i;
        tc->count = 0;

        char name[32];
        snprintf(name, sizeof(name), "stress_cancel_%d", i);
        char* name_dup = strdup(name);

        struct task_node* node = tasker_task_init_li(0, 1, name_dup, task_count, tc);
        if (node) {
            int ret = tasker_enqueue(node);
            if (ret == TASK_OK) {
                success++;
                // 立即取消一半
                if (i % 2 == 0) {
                    tasker_cancel_by_name(name_dup);
                }
            } else {
                free(node);
                free(tc);
                free(name_dup);
            }
        }
    }
    printf("[stress_cancel] enqueued %d/20 tasks, half cancelled\n", success);
}

// 压力测试6: 超时风暴（大量超时任务）
void stress_timeout_storm(void) {
    printf("\n========== Stress 6: Timeout Storm (15 timeout tasks) ==========\n");
    int success = 0;
    for (int i = 0; i < 15; i++) {
        struct test_ctx* tc = malloc(sizeof(struct test_ctx));
        tc->id = 6000 + i;
        tc->count = 0;

        char name[32];
        snprintf(name, sizeof(name), "stress_timeout_%d", i);
        char* name_dup = strdup(name);

        // little 级别，默认超时50ms，任务跑200ms，必然超时
        struct task_node* node = tasker_task_init_li(0, 1, name_dup, task_timeout, tc);
        if (node) {
            int ret = tasker_enqueue(node);
            if (ret == TASK_OK) success++;
            else {
                free(node);
                free(tc);
                free(name_dup);
            }
        }
    }
    printf("[stress_timeout] enqueued %d/15 timeout tasks\n", success);
}

// 压力测试7: 调度表满 + 重试
void stress_sched_full_retry(void) {
    printf("\n========== Stress 7: Sched Full + Retry (50 tasks) ==========\n");
    int success = 0;
    int full_count = 0;
    for (int i = 0; i < 50; i++) {
        struct test_ctx* tc = malloc(sizeof(struct test_ctx));
        tc->id = 7000 + i;
        tc->count = 0;

        char name[32];
        snprintf(name, sizeof(name), "stress_retry_%d", i);
        char* name_dup = strdup(name);

        struct task_node* node = tasker_task_init_li(0, 1, name_dup, task_count, tc);
        if (node) {
            int ret = tasker_enqueue(node);
            if (ret == TASK_QUEUE_FULL) {
                full_count++;
                // 等一会重试
                usleep(50 * 1000);
                ret = tasker_enqueue(node);
                if (ret == TASK_OK) success++;
                else {
                    free(node);
                    free(tc);
                    free(name_dup);
                }
            } else if (ret == TASK_OK) {
                success++;
            } else {
                free(node);
                free(tc);
                free(name_dup);
            }
        }
    }
    printf("[stress_retry] enqueued %d/50 tasks (full_count=%d)\n", success, full_count);
}

static int g_test_passed = 0;
static int g_test_failed = 0;
static int g_test_total = 0;

#define TEST_START(name) do { \
    g_test_total++; \
    printf("\n[%d/%d] %s ... ", g_test_total, 22, name); \
} while(0)

#define TEST_PASS() do { \
    g_test_passed++; \
    printf("PASS\n"); \
} while(0)

#define TEST_FAIL(reason) do { \
    g_test_failed++; \
    printf("FAIL (%s)\n", reason); \
} while(0)

void app_main(void)
{
    printf("\n========================================\n");
    printf("  ESP32-S3 SYSTEM START\n");
    printf("========================================\n");

    // 初始化 NVS（Wi-Fi 驱动依赖 NVS 存储配置）
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);
    printf("NVS initialized\n");

    // 初始化 Wi-Fi（AP+STA 共存模式）
    wifi_init();

    // 初始化 tasker 系统
    int ret = tasker_init();
    if (ret != 0) {
        printf("tasker_init failed: %d\n", ret);
        return;
    }

    // 启动 Web 服务器任务（SPIFFS 初始化 + Mongoose）
    // 注意：web_server_task 内部会轮询，不会返回，所以用独立任务运行
    TaskHandle_t web_task_handle = NULL;
    xTaskCreatePinnedToCore(
        web_server_task,
        "web_server",
        8192,
        NULL,
        5,
        &web_task_handle,
        1  // 在 CPU1 上运行
    );
    if (web_task_handle == NULL) {
        printf("Failed to create web server task\n");
    } else {
        printf("Web server task created on CPU1\n");
    }

    printf("\n========================================\n");
    printf("  TASKER FULL TEST SUITE START\n");
    printf("========================================\n");

    // ===== 基础功能测试 =====
    printf("\n");
    printf("========================================\n");
    printf("  BASIC FUNCTIONAL TESTS (1-15)\n");
    printf("========================================\n");

    TEST_START("Null Parameter");
    test_null_param();
    TEST_PASS();

    TEST_START("Cancel Non-existent");
    test_cancel_nonexist();
    TEST_PASS();

    TEST_START("Check Status");
    test_check_status();
    TEST_PASS();

    TEST_START("Basic Immediate");
    test_basic_immediate();
    TEST_PASS();

    TEST_START("Task Fail");
    test_task_fail();
    TEST_PASS();

    TEST_START("Cancel Task");
    test_cancel_task();
    TEST_PASS();

    TEST_START("Multi Task");
    test_multi_task();
    TEST_PASS();

    TEST_START("Priority Mix");
    test_priority_mix();
    TEST_PASS();

    TEST_START("Delayed Schedule");
    test_delayed_schedule();
    TEST_PASS();

    TEST_START("Timeout Detection");
    test_timeout_detection();
    TEST_PASS();

    TEST_START("Level Upgrade");
    test_level_upgrade();
    TEST_PASS();

    TEST_START("Lots Level");
    test_lots_level();
    TEST_PASS();

    TEST_START("Periodic Task");
    test_periodic_task();
    TEST_PASS();

    TEST_START("Cancel Periodic");
    test_cancel_periodic();
    TEST_PASS();

    TEST_START("Sched Full");
    test_sched_full();
    TEST_PASS();

    // 等基础测试跑完
    printf("\n[WAIT] waiting for basic tasks to complete...\n");
    usleep(2000 * 1000);

    // ===== 压力测试 =====
    printf("\n");
    printf("========================================\n");
    printf("  STRESS TESTS (16-22)\n");
    printf("========================================\n");

    TEST_START("Bulk Immediate (50 tasks)");
    stress_bulk_immediate();
    TEST_PASS();
    usleep(500 * 1000);

    TEST_START("Mixed Levels (30 tasks)");
    stress_mixed_levels();
    TEST_PASS();
    usleep(500 * 1000);

    TEST_START("Burst Submit (60 tasks)");
    stress_burst_submit();
    TEST_PASS();
    usleep(500 * 1000);

    TEST_START("Periodic Storm (10 tasks)");
    stress_periodic_storm();
    TEST_PASS();
    usleep(500 * 1000);

    TEST_START("Cancel Storm (20 tasks)");
    stress_cancel_storm();
    TEST_PASS();
    usleep(500 * 1000);

    TEST_START("Timeout Storm (15 tasks)");
    stress_timeout_storm();
    TEST_PASS();
    usleep(500 * 1000);

    TEST_START("Sched Full Retry (50 tasks)");
    stress_sched_full_retry();
    TEST_PASS();

    printf("\n========================================\n");
    printf("  TEST SUMMARY\n");
    printf("  Total: %d, Passed: %d, Failed: %d\n", g_test_total, g_test_passed, g_test_failed);
    printf("  (check serial output for detailed results)\n");
    printf("========================================\n");
}
