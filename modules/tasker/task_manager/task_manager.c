#include "task_manager.h"

struct task_manager* task_manager_init(const unsigned int size){

	int i = 0;
	struct task_manager* manager = (struct task_manager*)malloc(sizeof(struct task_manager));
	if (!manager){
		ESP_LOGE(TASK_MANAGER_TAG, "manager malloc fail!");
		return NULL;
	}
	struct task_node* queue = (struct task_node*)malloc(size * sizeof(struct task_node));
	if (!queue){
		ESP_LOGE(TASK_MANAGER_TAG, "queue malloc fail!");
		free(manager);
		return NULL;
	}

	for (; i < size; ++i){
		queue[i].fn = NULL;
		queue[i].pri = last;
		queue[i].timeout = 0;
		queue[i].cancel = 1;
		queue[i].ctx = NULL;
		queue[i].done = 0;
		queue[i].is_timeout = 0;
		queue[i].level = little;
		queue[i].name = "default";
	}
	manager->queue = queue;
	manager->is_empty = 1;
	manager->is_full = 0;
	manager->size = size;
	manager->running = 0;
	return manager;
}

struct task_node* task_init(const int timeout, 
									const uint64_t inject_time,
									const int period, 
									const int run_cnt, 
									const enum task_priority pri, 
									const enum task_time_cost_level level, 
									const char* name, 
									task_fn fn, void* ctx){

	struct task_node* node = (struct task_node*)malloc(sizeof(struct task_node));
	if (!node){
		ESP_LOGE(TASK_MANAGER_TAG, "task node malloc fail!");
		return NULL;
	}
	
	if (!fn){
		ESP_LOGE(TASK_MANAGER_TAG, "no task function.");
		return NULL;
	}

	node->inject_time = inject_time;
	node->fn = fn;
	node->pri = pri;
	node->timeout = timeout;
	node->cancel = 0;
	node->ctx = ctx;
	node->done = 0;
	node->is_timeout = 0;
	node->level = level;
	node->name = name;
	node->run_cnt = run_cnt;
	node->period = period;

	return node;
}


void task_done(struct task_node* node){
	node->done = 1;
}

int task_is_done(struct task_node* node){
	return node->done;
}

void task_cancel(struct task_node* node){
	node->cancel = 1;
}

int task_is_cancel(struct task_node* node){
	return node->cancel;
}


// Utils functions:

void task_node_pri_up(struct task_node* node){
	if (node->pri >= first) --node->pri;
}

void task_node_leve_up(struct task_node* node){
	if (node->level <= lots) ++node->level;
}

struct task_node* find_task_node_by_name(struct task_manager* worker_queue, const char* name){
	int size = worker_queue->size;
	for (int i = 0; i < size; ++i){
		if (!strcmp(worker_queue->queue[i].name, name)) return &(worker_queue->queue[i]);
	}

	ESP_LOGW(TASK_MANAGER_TAG, "Not found %s in worker_queue", name);
	return NULL;

}

void task_manager_pri_sort(struct task_manager* worker_queue){
	if (worker_queue->size <=1 ) return;

	int count[4] = {0};

	for (int i = 0; i < worker_queue->size; ++i){
		++count[worker_queue->queue[i].pri];
	}

	int start[4];
	start[1] = 0;
	start[2] = count[1];
	start[3] = count[1] + count[2];

	struct task_node* temp = (struct task_node*)malloc(worker_queue->size * sizeof(struct task_node));
	if (!temp) {
		printf("%s: worker queue sort fail", TASK_MANAGER_TAG);
		return;
	}

	int pos[4] = {start[1], start[2], start[3]};
	for (int i = 0; i < worker_queue->size; ++i){
		int p = worker_queue->queue[i].pri;
		temp[pos[p]++] = worker_queue->queue[i];
	}

	memcpy(worker_queue->queue, temp, worker_queue->size * sizeof(struct task_node));
	free(temp);
}



