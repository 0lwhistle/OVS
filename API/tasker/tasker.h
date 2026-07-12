#ifndef TASKER_H
#define TASKER_H

#include "task_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the tasker scheduler system
 * 
 * Must be called once before using any other tasker API.
 * If not called explicitly, it will be auto-initialized on first use.
 * 
 * @return TASK_OK on success, error code otherwise
 */
int tasker_init(void);

/**
 * @brief Enqueue a task node into the scheduler
 * @param node Task node to enqueue
 * @return TASK_OK on success, error code otherwise
 */
int tasker_enqueue(struct task_node* node);

/**
 * @brief Cancel a task by its node pointer
 * @param node Task node to cancel
 */
void tasker_cancel_by_node(struct task_node* node);

/**
 * @brief Cancel a task by its name
 * @param name Task name to cancel
 */
void tasker_cancel_by_name(const char* name);

/**
 * @brief Check if the scheduler queue is full
 * @return 1 if full, 0 otherwise
 */
int tasker_is_full(void);

/**
 * @brief Check if the scheduler queue is empty
 * @return 1 if empty, 0 otherwise
 */
int tasker_is_empty(void);

/**
 * @brief Initialize a high-priority (little) scheduled task
 * 
 * Default timeout: LITTLE_TASK_DEFAULT_TIMEOUT (50ms)
 * Suitable for short, fast tasks.
 * 
 * @param period Task period in ms (0 for one-shot, >0 for periodic)
 * @param run_cnt Number of runs (-1 for infinite)
 * @param name Task name (must be unique)
 * @param fn Task callback function
 * @param ctx User context pointer
 * @return Pointer to task_node, or NULL on failure
 */
struct task_node* tasker_task_init_li(
    const int period, 
    const int run_cnt, 
    const char* name, 
    task_fn fn, void* ctx
);

/**
 * @brief Initialize a medium-priority (middle) scheduled task
 * 
 * Default timeout: MIDDLE_TASK_DEFAULT_TIMEOUT (1000ms)
 * Suitable for normal tasks.
 * 
 * @param period Task period in ms (0 for one-shot, >0 for periodic)
 * @param run_cnt Number of runs (-1 for infinite)
 * @param name Task name (must be unique)
 * @param fn Task callback function
 * @param ctx User context pointer
 * @return Pointer to task_node, or NULL on failure
 */
struct task_node* tasker_task_init_mi( 
    const int period, 
    const int run_cnt, 
    const char* name, 
    task_fn fn, void* ctx
);

/**
 * @brief Initialize a low-priority (lots) scheduled task
 * 
 * Suitable for long-running or heavy tasks.
 * 
 * @param timeout Task timeout in ms
 * @param period Task period in ms (0 for one-shot, >0 for periodic)
 * @param run_cnt Number of runs (-1 for infinite)
 * @param name Task name (must be unique)
 * @param fn Task callback function
 * @param ctx User context pointer
 * @return Pointer to task_node, or NULL on failure
 */
struct task_node* tasker_task_init_lo(
    const int timeout,
    const int period, 
    const int run_cnt, 
    const char* name, 
    task_fn fn, void* ctx
);

#ifdef __cplusplus
}
#endif

#endif // TASKER_H
