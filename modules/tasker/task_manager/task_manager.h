#ifndef TASK_MANAGER
#define TASK_MANAGER

#include <stdlib.h>
#include <stdio.h>

#define LITTLE_TASK_QUEUE_SIZE 4
#define MIDDEL_TASK_QUEUE_SIZE 8
#define LOTS_TASK_QUEUE_SIZE 8
#define DISPATCHER_TASK_QUEUE_SIZE 32
#define SCHED_TASK_QUEUE_SIZE 32

#define LITTLE_TASK_STACK_SIZE 3072
#define MIDDEL_TASK_STACK_SIZE 4096
#define LOTS_TASK_STACK_SIZE 8192
#define DISPATCHER_TASK_QUEUE_STACK_SIZE 4096
#define SCHED_TASK_QUEUE_STACK_SIZE 4096

#define LITTLE_TASK_default_timeout 50 // ms
#define MIDDEL_TASK_default_timeout 1000 // ms
#define LOTS_TASK_default_timeout 10000 // ms

static const char* TASK_MANAGER_TAG = "[TASK_MANAGER]";

static int mutex = 0; 

enum task_t{
	TASK_OK = 0,
	TASK_MEM_ERR = -1,
	TASK_TIMEOUT_ERR = -2,
	TASK_PARA_ERR = -3,
	TASK_FUNC_ERR = -4,
	TASK_INNER_ERR = -5,

};

enum task_priority{
	frist = 1,
	middle,
	last,
};

enum task_time_cost_level{
	little = 1,
	middle,
	lots,
};

typedef enum task_t (*task_fn)(void* ctx);

struct task_node{
	int done;
	int cancel;
	int timeout;
	int is_timeout;
	enum task_priority pri;
	enum task_time_cost_level level;
	task_fn fn;
	char* name;
	void* ctx;
};

struct task_manager{
	int running;
	int is_full;
	int is_empty;
	unsigned int size;
	struct task_node* queue;
};

static struct task_manager* task_manager_init(const unsigned int size);

static struct task_node* task_notimeout_init(task_fn fn, void* ctx);
static struct task_node* task_init(const int timeout, const enum task_priority pri, const enum task_time_cost_level level, const char* name, task_fn fn, void* ctx);

static inline void task_done(struct task_node* node);
static inline int task_is_done(struct task_node* node);

static inline void task_cancel(struct task_node* node);
static inline int task_is_cancel(struct task_node* node);


// Utils functions:
static inline struct task_node* find_task_node_by_name(struct task_manager* worker_queue, const char* name);
static inline void task_manager_pri_sort(struct task_manager* worker_queue);
static inline void task_node_pri_up(struct task_node* node);


#endif // TASK_MANAGER