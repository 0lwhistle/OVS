#ifndef TASK_WORKER
#define TASK_WORKER

#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gptimer.h"

#include "task_manager.h"
#include "logger.h"

extern const char* TASK_WORKER_TAG;
static int worker_init_flag = 0;

struct task_worker_ctx{
	struct task_worker* little_worker;
	struct task_worker* middle_worker;
	struct task_worker* lots_worker;
	struct task_worker* s_dispatcher;
	struct task_worker* s_sched_table;
};

extern struct task_worker_ctx s_task_worker_ctx;

typedef void (*worker_handler_fn)(struct task_worker*);
typedef int (*enqueue_fn)(struct task_node*);
typedef int (*dequeue_fn)(struct task_node*);
typedef struct task_node* (*find_by_name_fn)(struct task_node*);

struct task_worker{
	int stop;
	pthread_t pt;
	pthread_mutex_t mtx;
	pthread_cond_t cond;

	struct task_manager* worker_queue; // save and manage the tasks
};

int worker_init(void);
void worker_delete(struct task_worker* worker);

int worker_little_init(void);
int worker_middle_init(void);
int worker_lots_init(void);
int worker_dispatcher_init(void);
int worker_sched_init(void);

// the handler to be register for the workers (pthread_create expects void* (*)(void*))
void* worker_little_handler(void* arg);
void* worker_middle_handler(void* arg);
void* worker_lots_handler(void* arg);
void* worker_dispatcher_handler(void* arg);
void* worker_sched_handler(void* arg);
void worker_do_handler(struct task_worker* worker);

esp_timer_handle_t* timeout_timer_init(const int timeout, void* arg);

int worker_task_enqueue(struct task_worker* worker, struct task_node* node);
void worker_task_done(struct task_worker* worker, struct task_node* node);
void worker_task_cancel(struct task_worker* worker, struct task_node* node);

// API
int shched_enqueue(struct task_node* node);
void shched_cancel_by_node(struct task_node* node);
void shched_cancel_by_name(const char* name);
int shched_is_full(void);
int shched_is_empty(void);

struct task_node* sched_task_init_li(
									const int period, 
									const int run_cnt, 
									const char* name, 
									task_fn fn, void* ctx
								);

struct task_node* sched_task_init_mi( 
									const int period, 
									const int run_cnt, 
									const char* name, 
									task_fn fn, void* ctx
								);

struct task_node* sched_task_init_lo(
									const int timeout,
									const int period, 
									const int run_cnt, 
									const char* name, 
									task_fn fn, void* ctx
								);
#endif // __TASK_WORKER_H__
