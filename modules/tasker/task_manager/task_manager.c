#include "task_manager.h"

static struct task_manager* task_manager_init(const unsigned int size){

	int i = 0;
	struct task_manager* manager = (struct task_manager*)malloc(sizeof(struct task_manager));
	if (!manager){
		ESP_LOGE(TAG, "manager malloc fail!");
		return NULL;
	}
	struct task_node* queue = (struct task_node*)malloc(size * sizeof(struct task_node));
	if (!queue){
		ESP_LOGE(TAG, "queue malloc fail!");
		return NULL;
	}
	manager->queue = queue;
	manager->is_empty = 1;
	manager->is_full = 0;
	manager->size = size;


	return manager;
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