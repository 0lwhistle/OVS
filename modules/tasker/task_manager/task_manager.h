#ifndef TASK_MANAGER
#define TASK_MANAGER

#include <stdlib.h>
#include <stdio.h>

#define LITTLE_TASK_QUEUE_SIZE 4
#define MIDDEL_TASK_QUEUE_SIZE 8
#define LOTS_TASK_QUEUE_SIZE 8

#define LITTLE_TASK_STACK_SIZE 3072
#define MIDDEL_TASK_STACK_SIZE 4096
#define LOTS_TASK_STACK_SIZE 8192

#define LITTLE_TASK_default_timeout 50
#define MIDDEL_TASK_default_timeout 1000
#define LOTS_TASK_default_timeout 10000

static const char *TAG = "[TASK_MANAGER]";

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
	int in_worker;
	int done;
	int cancel;
	unsigned int timeout;
	enum task_priority pri;
	task_fn fn;
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

static int get_mutex(struct task_node* node);
static int put_mutex(struct task_node* node);

static int task_notimeout_init(task_fn fn, void* ctx);
static int task_init(const int timeout, task_fn fn, void* ctx);

static inline void task_done(struct task_node* node);
static inline int task_is_done(struct task_node* node);

static inline void task_cancel(struct task_node* node);
static inline int task_is_cancel(struct task_node* node);

static inline int task_is_in_worker(struct task_node* node);

static struct task_node* task_pop(struct task_manager* tkm);




#endif // TASK_MANAGER