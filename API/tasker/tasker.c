#include "tasker.h"
#include "task_worker.h"
#include "logger.h"
#include <string.h>

/* get current time in milliseconds */
static inline uint64_t tasker_get_time_ms(void){
    return esp_timer_get_time() / 1000;
}

/* auto-initialize if not already done */
static inline int tasker_auto_init(void){
    if (!worker_init_flag){
        int ret = worker_init();
        if (ret != 0) {
            printf("tasker_init failed: %d\n", ret);
            return TASK_INNER_ERR;
        }
    }
    return TASK_OK;
}

/* validate common task parameters */
static inline int tasker_validate_params(const int period, task_fn fn, const char* name){
    if (period < 0 || !fn || !name || !strlen(name)){
        LOGW(TASK_WORKER_TAG, "tasker_task_init fail, invalid params");
        return TASK_PARA_ERR;
    }
    return TASK_OK;
}

int tasker_init(void){
    return tasker_auto_init();
}

int tasker_enqueue(struct task_node* node){
    if (!node) {
        LOGW(TASK_WORKER_TAG, "tasker_enqueue fail, node is null");
        return TASK_PARA_ERR;
    }

    if (node->cancel || node->done || node->period < 0 || !node->fn || !strlen(node->name)){
        LOGW(TASK_WORKER_TAG, "tasker_enqueue fail, node is invalid");
        return TASK_PARA_ERR;
    }

    int ret = tasker_auto_init();
    if (ret != TASK_OK) return ret;

    ret = worker_task_enqueue(s_task_worker_ctx.s_sched_table, node);
    // Move semantics: on success, invalidate the source so caller can't reuse
    if (ret == TASK_OK) {
        node->fn = NULL;
    }
    return ret;
}

void tasker_cancel_by_node(struct task_node* node){
    if (!node) return;
    tasker_cancel_by_name(node->name);
}

void tasker_cancel_by_name(const char* name){
    if (!name || !strlen(name)) return;

    pthread_mutex_lock(&(s_task_worker_ctx.s_sched_table->mtx));
    struct task_node* node = find_task_node_by_name(s_task_worker_ctx.s_sched_table->worker_queue, name);
    if (node) task_cancel(node);
    pthread_mutex_unlock(&(s_task_worker_ctx.s_sched_table->mtx));
}

int tasker_is_full(void){
    return task_manager_is_full(s_task_worker_ctx.s_sched_table->worker_queue) ? 1 : 0;
}

int tasker_is_empty(void){
    return task_manager_is_empty(s_task_worker_ctx.s_sched_table->worker_queue) ? 1 : 0;
}

int tasker_task_init_li( 
                                struct task_node* out,
                                const int period, 
                                const int run_cnt, 
                                const char* name, 
                                task_fn fn, void* ctx
                            ){
    if (!out) return TASK_PARA_ERR;
    if (tasker_validate_params(period, fn, name) != TASK_OK) return TASK_PARA_ERR;
    if (tasker_auto_init() != TASK_OK) return TASK_INNER_ERR;

    task_node_init(out, LITTLE_TASK_DEFAULT_TIMEOUT, tasker_get_time_ms(), 
                   period, run_cnt, last, level_little, name, fn, ctx);
    return TASK_OK;
}

int tasker_task_init_mi(
                                struct task_node* out,
                                const int period, 
                                const int run_cnt, 
                                const char* name, 
                                task_fn fn, void* ctx
                            ){
    if (!out) return TASK_PARA_ERR;
    if (tasker_validate_params(period, fn, name) != TASK_OK) return TASK_PARA_ERR;
    if (tasker_auto_init() != TASK_OK) return TASK_INNER_ERR;

    task_node_init(out, MIDDLE_TASK_DEFAULT_TIMEOUT, tasker_get_time_ms(), 
                   period, run_cnt, last, level_middle, name, fn, ctx);
    return TASK_OK;
}

int tasker_task_init_lo(struct task_node* out,
                                const int timeout, 
                                const int period, 
                                const int run_cnt, 
                                const char* name, 
                                task_fn fn, void* ctx
                            ){
    if (!out) return TASK_PARA_ERR;
    if (tasker_validate_params(period, fn, name) != TASK_OK) return TASK_PARA_ERR;
    if (tasker_auto_init() != TASK_OK) return TASK_INNER_ERR;

    task_node_init(out, timeout, tasker_get_time_ms(), 
                   period, run_cnt, last, level_lots, name, fn, ctx);
    return TASK_OK;
}
