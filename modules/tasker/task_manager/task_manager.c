#include "task_manager.h"

static struct task_manager* task_manager_init(const unsigned int size){

	int i = 0;
	struct task_manager* manager = (struct task_manager*)malloc(sizeof(struct task_manager));
	if (!manager){
		ESP_LOGE(TASK_MANAGER_TAG, "manager malloc fail!");
		return NULL;
	}
	struct task_node* queue = (struct task_node*)malloc(size * sizeof(struct task_node));
	if (!queue){
		ESP_LOGE(TASK_MANAGER_TAG, "queue malloc fail!");
		return NULL;
	}
	manager->queue = queue;
	manager->is_empty = 1;
	manager->is_full = 0;
	manager->size = size;


	return manager;
}

static int task_init(const int timeout, const enum task_priority pri, task_fn fn, void* ctx){

	struct task_node* node = (struct task_node*)malloc(sizeof(struct task_node));
	if (!node){
		ESP_LOGE(TASK_MANAGER_TAG, "task node malloc fail!");
		return TASK_MEM_ERR;
	}
	
	if (!fn){
		ESP_LOGE(TASK_MANAGER_TAG, "no task function.");
		return TASK_FUNC_ERR;
	}
	node->fn = fn;
	node->pri = pri;
	node->timeout = timeout;
	node->cancel = 0;
	node->ctx = ctx;
	node->done = 0;
	node->in_worker = 0;

	return 0;
}


static inline void task_done(struct task_node* node){
	node->done = 1;
}

static inline int task_is_done(struct task_node* node){
	return node->done;
}

static inline void task_cancel(struct task_node* node){
	node->cancel = 1;
}

static inline int task_is_cancel(struct task_node* node){
	return node->cancel;
}

static inline int task_is_in_worker(struct task_node* node){
	return node->in_worker;
}