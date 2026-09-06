#ifndef TASK_MANAGER
#define TASK_MANAGER

#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gptimer.h"

#define LITTLE_TASK_QUEUE_SIZE 8
#define MIDDLE_TASK_QUEUE_SIZE 8
#define LOTS_TASK_QUEUE_SIZE 8
#define DISPATCHER_TASK_QUEUE_SIZE 64
#define SCHED_TASK_QUEUE_SIZE 64

#define LITTLE_TASK_STACK_SIZE 3072
#define MIDDLE_TASK_STACK_SIZE 4096
#define LOTS_TASK_STACK_SIZE 8192
#define DISPATCHER_TASK_QUEUE_STACK_SIZE 4096
#define SCHED_TASK_QUEUE_STACK_SIZE 4096

#define LITTLE_TASK_DEFAULT_TIMEOUT 50 // ms
#define MIDDLE_TASK_DEFAULT_TIMEOUT 1000 // ms
#define LOTS_TASK_DEFAULT_TIMEOUT 10000 // ms

extern const char* TASK_MANAGER_TAG;

enum task_t{
	TASK_OK = 0,
	TASK_MEM_ERR = -1,
	TASK_TIMEOUT_ERR = -2,
	TASK_PARA_ERR = -3,
	TASK_FUNC_ERR = -4,
	TASK_INNER_ERR = -5,
	TASK_QUEUE_FULL = -6,
	TASK_STOP = -7,
};

enum task_priority{
	first = 1,
	middle,
	last,
};

enum task_time_cost_level{
	level_little = 1,
	level_middle,
	level_lots,
};

typedef enum task_t (*task_fn)(void* ctx);

struct task_node{
	int done;
	int cancel;
	int timeout;
	int is_timeout;
	int period;
	int run_cnt;
	enum task_priority pri;
	enum task_time_cost_level level;
	task_fn fn;
	uint64_t inject_time;
	char name[32];
	void* ctx;
	int dispatched; /* 1 while the node is borrowed by dispatcher/worker */
};

struct task_manager{
	unsigned int size;
	struct task_node** queue;
};

// Runtime-computed helpers (no stale flags)
bool task_manager_is_empty(const struct task_manager* mgr);
bool task_manager_is_full(const struct task_manager* mgr);

struct task_manager* task_manager_init(const unsigned int size);

struct task_node* task_node_pool_alloc(void);
void task_node_pool_free(struct task_node* node);

// In-place initialization (no heap allocation)
void task_node_init(struct task_node* node,
					const int timeout,
					const uint64_t inject_time,
					const int period, 
					const int run_cnt, 
					const enum task_priority pri, 
					const enum task_time_cost_level level, 
					const char* name, 
					task_fn fn, void* ctx);

void task_node_pri_up(struct task_node* node);

void task_node_leve_up(struct task_node* node);

struct task_node* find_task_node_by_name(struct task_manager* worker_queue, const char* name);

void task_manager_pri_sort(struct task_manager* worker_queue);

void task_done(struct task_node* node);

int task_is_done(struct task_node* node);

void task_cancel(struct task_node* node);

int task_is_cancel(struct task_node* node);


#endif // TASK_MANAGER
