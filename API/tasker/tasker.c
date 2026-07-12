#include "tasker.h"
#include "task_worker.h"
#include "logger.h"

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
        LOGW(TASK_WORKER_TAG, "tasker_enqueue fail, node is invaild");
        return TASK_PARA_ERR;
    }

    int ret = tasker_auto_init();
    if (ret != TASK_OK) return ret;

    return worker_task_enqueue(s_task_worker_ctx.s_sched_table, node);
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
    return s_task_worker_ctx.s_sched_table->worker_queue->is_full;
}

int tasker_is_empty(void){
    return s_task_worker_ctx.s_sched_table->worker_queue->is_empty;
}

struct task_node* tasker_task_init_li( 
                                const int period, 
                                const int run_cnt, 
                                const char* name, 
                                task_fn fn, void* ctx
                            ){
    if (tasker_validate_params(period, fn, name) != TASK_OK) return NULL;
    if (tasker_auto_init() != TASK_OK) return NULL;

    return task_init(LITTLE_TASK_DEFAULT_TIMEOUT, tasker_get_time_ms(), 
                     period, run_cnt, last, level_little, name, fn, ctx);
}

struct task_node* tasker_task_init_mi(
                                const int period, 
                                const int run_cnt, 
                                const char* name, 
                                task_fn fn, void* ctx
                            ){
    if (tasker_validate_params(period, fn, name) != TASK_OK) return NULL;
    if (tasker_auto_init() != TASK_OK) return NULL;

    return task_init(MIDDLE_TASK_DEFAULT_TIMEOUT, tasker_get_time_ms(), 
                     period, run_cnt, last, level_middle, name, fn, ctx);
}

struct task_node* tasker_task_init_lo(const int timeout, 
                                const int period, 
                                const int run_cnt, 
                                const char* name, 
                                task_fn fn, void* ctx
                            ){
    if (tasker_validate_params(period, fn, name) != TASK_OK) return NULL;
    if (tasker_auto_init() != TASK_OK) return NULL;

    return task_init(timeout, tasker_get_time_ms(), 
                     period, run_cnt, last, level_lots, name, fn, ctx);
}
