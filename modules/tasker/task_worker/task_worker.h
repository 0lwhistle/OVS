#ifndef TASK_WORKER
#define TASK_WORKER

#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "task_manager.h"

static const char *TAG = "[TASK_WORKER]";


struct task_worker{
	int is_working;
	int stop;
	pthread_t pt;
	pthread_mutex_t mtx;
	pthread_cond_t cond;
	struct task_manager* worker_queue;
};

struct task_manager* s_worker_queue;


static int worker_init(void);
static int worker_little_init(struct task_worker* worker);
static int worker_middle_init(struct task_worker* worker);
static int worker_lots_init(struct task_worker* worker);

static void worker_little_handler(void);
static void worker_middle_handler(void);
static void worker_lots_handler(void);
static void worker_handler(void);

static void timer_callback(void* arg);

static worker_task_enqueue(struct task_worker* worker, struct task_node* node);
static worker_task_pop(struct task_worker* worker, struct task_node* node);



#endif // __TASK_WORKER_H__