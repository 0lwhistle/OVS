/**
 * @file app_init.h
 * @brief 应用初始化注册表（REFACTORING_PLAN 4.4：holder 依赖拓扑编排）
 *
 * main.c 只保留 NVS/SPIFFS 手工初始化，其余模块统一在此注册并由
 * holder 按 依赖分批 初始化（required 失败即停，optional 失败降级）。
 */

#ifndef APP_INIT_H
#define APP_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注入网络栈初始化函数（OTA 线保护区桥接）
 *
 * main.c 将 net_stack_init 的函数指针在此注入；未注入（OVS_ENABLE_NET=0）
 * 时注册表不含 net_stack 模块，语义与历史行为一致。
 */
void app_init_set_net_stack(void (*fn)(void));

/** 注册全部模块（幂等） */
int app_init_setup(void);

/** holder_init_all + 打印模块状态/订阅者清单（开机验收证据） */
int app_init_run(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_INIT_H */
