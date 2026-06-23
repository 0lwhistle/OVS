#ifndef TASK_WORKER
#define TASK_WORKER

#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "task_manager.h"

static const char* TASK_WORKER_TAG = "[TASK_WORKER]";

static struct task_worker s_worker_queue;

typedef void (*worker_handler)(struct task_worker*);

struct task_worker{
	int is_working;
	int stop;
	worker_handler handler;
	pthread_t pt;
	pthread_mutex_t mtx;
	pthread_cond_t cond;
	struct task_worker* src;
	struct task_manager* worker_queue;
};

static int worker_init(void);
static int worker_little_init(struct task_worker* worker);
static int worker_middle_init(struct task_worker* worker);
static int worker_lots_init(struct task_worker* worker);

static void worker_little_handler(void);
static void worker_middle_handler(void);
static void worker_lots_handler(void);
static void worker_handler(void);

static esp_timer_handle_t* timeout_timer_init(const int timeout, void* arg);
static void timer_callback(void* arg);

static int worker_task_enqueue(struct task_worker* worker, struct task_node* node);
static void worker_task_pop(struct task_worker* worker, struct task_node* node);

static inline void wakeup_s_worker_queue(struct task_worker* worker);



#endif // __TASK_WORKER_H__