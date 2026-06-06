#ifndef TASK_MANAGER
#define TASK_MANAGER

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

typedef task_t (*task_fn)(const int timeout, void* ctx);

struct task_node{
	int id;
	int done;
	task_priority pri;
	task_fn fn;
};

struct task_manager{
	int running;
	int is_full;
	int is_empty;
	struct task_node queue[64];
};


static int get_mutex(struct task_node* node);
static int put_mutex(struct task_node* node);

static int task_notimeout_init(task_fn fn, void* ctx);
static int task_init(const int timeout, task_fn fn, void* ctx);

static int task_is_done(struct task_node* node);

static task_node* task_pop(struct task_manager* tkm);




#endif // TASK_MANAGER