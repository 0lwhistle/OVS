/**
 * @file test_main.c
 * @brief ovs_tests 统一入口：core 层 PC 宿主测试
 *
 * mem_pool / event_bus / tasker —— 全部为可 PC 运行的纯逻辑模块
 * （REFACTORING_PLAN Phase 0a C8 + 5.1/5.2 门禁）
 */

#include <stdio.h>

void test_mem_run(int* pass, int* fail);
void test_event_bus_run(int* pass, int* fail);
void test_tasker_run(int* pass, int* fail);
void test_audio_run(int* pass, int* fail);
void test_nav_stack_run(int* pass, int* fail);
void test_hub_run(int* pass, int* fail);
void test_ath30_run(int* pass, int* fail);
void test_lora_tp_run(int* pass, int* fail);

int main(void) {
    int pass = 0, fail = 0;

    test_mem_run(&pass, &fail);
    test_event_bus_run(&pass, &fail);
    test_tasker_run(&pass, &fail);
    test_audio_run(&pass, &fail);
    test_nav_stack_run(&pass, &fail);
    test_hub_run(&pass, &fail);
    test_ath30_run(&pass, &fail);
    test_lora_tp_run(&pass, &fail);

    printf("\n==== ovs_tests TOTAL: %d passed, %d failed ====\n", pass, fail);
    return fail ? 1 : 0;
}
