/**
 * @file tasker.h
 * @brief Tasker 调度系统公共 API
 * 
 * 提供任务调度、优先级管理、超时控制等功能。
 * 外部模块只需包含此头文件即可使用 tasker 功能。
 */
#ifndef TASKER_H
#define TASKER_H

#include "task_manager.h"

#define TASK_CNT_INF -1

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 tasker 调度系统
 * 
 * 必须在使用其他 tasker API 之前调用。
 * 如果未显式调用，首次使用时会自动初始化。
 * 
 * @return TASK_OK 成功，其他值失败
 */
int tasker_init(void);

/**
 * @brief 将任务节点加入调度器 (移动语义)
 * 
 * 成功后，源节点会被置空 (fn 设为 NULL)，调用方无法复用。
 * 失败时，源节点保持不变，可以重试。
 * 
 * @param node 要加入调度器的任务节点
 * @return TASK_OK 成功，其他值失败
 */
int tasker_enqueue(struct task_node* node);

/**
 * @brief 通过节点指针取消任务
 * @param node 要取消的任务节点
 */
void tasker_cancel_by_node(struct task_node* node);

/**
 * @brief 通过任务名称取消任务
 * @param name 要取消的任务名称
 */
void tasker_cancel_by_name(const char* name);

/**
 * @brief 检查调度队列是否已满
 * @return 1 已满，0 未满
 */
int tasker_is_full(void);

/**
 * @brief 检查调度队列是否为空
 * @return 1 为空，0 非空
 */
int tasker_is_empty(void);

/**
 * @brief 初始化高优先级 (little) 任务
 * 
 * 默认超时: LITTLE_TASK_DEFAULT_TIMEOUT (50ms)
 * 适用于短小快速的任务。
 * 
 * @param out     调用方分配的任务节点指针
 * @param period  周期 (ms, 0 表示一次性)
 * @param run_cnt 运行次数 (-1 表示无限)
 * @param name    任务名称 (必须唯一)
 * @param fn      任务回调函数
 * @param ctx     用户上下文
 * @return TASK_OK 成功，其他值失败
 */
int tasker_task_init_li(struct task_node* out, int period, int run_cnt, 
                        const char* name, task_fn fn, void* ctx);

/**
 * @brief 初始化中等优先级 (middle) 任务
 * 
 * 默认超时: MIDDLE_TASK_DEFAULT_TIMEOUT (1000ms)
 * 适用于普通任务。
 * 
 * @param out     调用方分配的任务节点指针
 * @param period  周期 (ms, 0 表示一次性)
 * @param run_cnt 运行次数 (-1 表示无限)
 * @param name    任务名称 (必须唯一)
 * @param fn      任务回调函数
 * @param ctx     用户上下文
 * @return TASK_OK 成功，其他值失败
 */
int tasker_task_init_mi(struct task_node* out, int period, int run_cnt, 
                        const char* name, task_fn fn, void* ctx);

/**
 * @brief 初始化低优先级 (lots) 任务
 * 
 * 适用于长时间运行或重量级任务。
 * 
 * @param out     调用方分配的任务节点指针
 * @param timeout 超时时间 (ms)
 * @param period  周期 (ms, 0 表示一次性)
 * @param run_cnt 运行次数 (-1 表示无限)
 * @param name    任务名称 (必须唯一)
 * @param fn      任务回调函数
 * @param ctx     用户上下文
 * @return TASK_OK 成功，其他值失败
 */
int tasker_task_init_lo(struct task_node* out, int timeout, int period, 
                        int run_cnt, const char* name, task_fn fn, void* ctx);

#ifdef __cplusplus
}
#endif

#endif /* TASKER_H */
