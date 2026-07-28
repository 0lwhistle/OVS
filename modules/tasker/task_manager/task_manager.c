#include "task_manager.h"
#include <string.h>

const char* TASK_MANAGER_TAG = "[TASK_MANAGER]";

struct task_manager* task_manager_init(const unsigned int size){

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

	for (int i = 0; i < size; ++i){
		queue[i].fn = NULL;
		queue[i].pri = last;
		queue[i].timeout = 0;
		queue[i].cancel = 1;
		queue[i].ctx = NULL;
		queue[i].done = 0;
		queue[i].is_timeout = 0;
		queue[i].level = level_little;
		queue[i].name[0] = '\0';
	}
	manager->queue = queue;
	manager->size = size;
	return manager;
}

// In-place initialization on a caller-provided task_node (no heap allocation)
void task_node_init(struct task_node* node,
                    const int timeout,
                    const uint64_t inject_time,
                    const int period, 
                    const int run_cnt, 
                    const enum task_priority pri, 
                    const enum task_time_cost_level level, 
                    const char* name, 
                    task_fn fn, void* ctx)
{
	node->inject_time = inject_time;
	node->fn = fn;
	node->pri = pri;
	node->timeout = timeout;
	node->cancel = 0;
	node->ctx = ctx;
	node->done = 0;
	node->is_timeout = 0;
	node->level = level;
	node->period = period;
	node->run_cnt = run_cnt;
	strncpy(node->name, name, sizeof(node->name) - 1);
	node->name[sizeof(node->name) - 1] = '\0';
}

// Runtime-computed queue status (no stale flags)
bool task_manager_is_empty(const struct task_manager* mgr){
	for (int i = 0; i < mgr->size; ++i){
		if (!mgr->queue[i].cancel && !mgr->queue[i].done){
			return false;
		}
	}
	return true;
}

bool task_manager_is_full(const struct task_manager* mgr){
	for (int i = 0; i < mgr->size; ++i){
		if (mgr->queue[i].cancel || mgr->queue[i].done){
			return false;
		}
	}
	return true;
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
	if (node->pri > first) --node->pri;
}

void task_node_leve_up(struct task_node* node){
	if (node->level < level_lots) ++node->level;
}

struct task_node* find_task_node_by_name(struct task_manager* worker_queue, const char* name){
	int size = worker_queue->size;
	for (int i = 0; i < size; ++i){
		if (!worker_queue->queue[i].cancel && 
		    !worker_queue->queue[i].done &&
		    !strcmp(worker_queue->queue[i].name, name)) {
			return &(worker_queue->queue[i]);
		}
	}
	return NULL;
}

// Counting sort by priority, using VLA on stack (no heap allocation)
void task_manager_pri_sort(struct task_manager* worker_queue){
	int size = worker_queue->size;
	if (size <= 1) return;

	// pri: first=1, middle=2, last=3
	int count[4] = {0};
	for (int i = 0; i < size; ++i){
		int p = worker_queue->queue[i].pri;
		if (p >= 1 && p <= 3) ++count[p];
	}

	int start[4] = {0, 0, count[1], count[1] + count[2]};

	// VLA on stack — max size is SCHED_TASK_QUEUE_SIZE=64, ~5KB safe
	struct task_node temp[size];

	int pos[4] = {start[0], start[1], start[2], start[3]};
	for (int i = 0; i < size; ++i){
		int p = worker_queue->queue[i].pri;
		if (p < 1 || p > 3) continue;
		temp[pos[p]++] = worker_queue->queue[i];
	}

	memcpy(worker_queue->queue, temp, size * sizeof(struct task_node));
}
