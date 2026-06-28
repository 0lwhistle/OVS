#ifndef TASK_WORKER
#define TASK_WORKER

#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "task_manager.h"

static const char* TASK_WORKER_TAG = "[TASK_WORKER]";

struct task_worker_ctx{
	struct task_worker* little_worker;
	struct task_worker* middle_worker;
	struct task_worker* lots_worker;
	struct task_worker* s_dispatcher;
	struct task_worker* s_sched_table;
};

static struct task_worker_ctx s_task_worker_ctx = {0};

typedef void (*worker_handler_fn)(struct task_worker*);

struct task_worker{
	int stop;
	pthread_t pt;
	pthread_mutex_t mtx;
	pthread_cond_t cond;
	struct task_manager* worker_queue; // save and manage the tasks
};

static int worker_init(void);
static void worker_delete(struct task_worker* worker);

static int worker_little_init(void);
static int worker_middle_init(void);
static int worker_lots_init(void);
static int worker_dispatcher_init(void);
static int worker_sched_init(void);



// the handler to be register for the workers
static void worker_little_handler(struct task_worker* worker);
static void worker_middle_handler(struct task_worker* worker);
static void worker_lots_handler(struct task_worker* worker);
static void worker_dispatcher_handler(struct task_worker* worker);
static void worker_sched_handler(struct task_worker* worker);


static esp_timer_handle_t* timeout_timer_init(const int timeout, void* arg);
static void timer_callback(void* arg);

static int worker_task_enqueue(struct task_worker* worker, struct task_node* node);
static void worker_task_done(struct task_worker* worker, struct task_node* node);
static void worker_task_cancel(struct task_worker* worker, struct task_node* node);



#endif // __TASK_WORKER_H__