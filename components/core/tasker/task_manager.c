#include "task_manager.h"
#include "mem.h"
#include <string.h>

const char* TASK_MANAGER_TAG = "[TASK_MANAGER]";

#define TASK_NODE_POOL_SIZE SCHED_TASK_QUEUE_SIZE

static struct task_node s_task_node_pool[TASK_NODE_POOL_SIZE];
static struct task_node* s_task_node_free_stack[TASK_NODE_POOL_SIZE];
static int s_task_node_free_count;
static int s_task_node_pool_ready;
static pthread_mutex_t s_task_node_pool_mtx = PTHREAD_MUTEX_INITIALIZER;

static void task_node_pool_init_locked(void){
	if (s_task_node_pool_ready) return;
	for (int i = 0; i < TASK_NODE_POOL_SIZE; ++i){
		s_task_node_free_stack[s_task_node_free_count++] = &s_task_node_pool[i];
	}
	s_task_node_pool_ready = 1;
}

struct task_node* task_node_pool_alloc(void){
	pthread_mutex_lock(&s_task_node_pool_mtx);
	task_node_pool_init_locked();
	if (s_task_node_free_count <= 0){
		pthread_mutex_unlock(&s_task_node_pool_mtx);
		return NULL;
	}
	struct task_node* node = s_task_node_free_stack[--s_task_node_free_count];
	pthread_mutex_unlock(&s_task_node_pool_mtx);

	memset(node, 0, sizeof(*node));
	node->cancel = 0;
	node->done = 0;
	node->dispatched = 0;
	return node;
}

void task_node_pool_free(struct task_node* node){
	if (!node || node->dispatched != 0) return;
	pthread_mutex_lock(&s_task_node_pool_mtx);
	task_node_pool_init_locked();
	if (s_task_node_free_count < TASK_NODE_POOL_SIZE){
		s_task_node_free_stack[s_task_node_free_count++] = node;
		node->cancel = 1;
		node->done = 1;
		node->dispatched = -1; /* free-list sentinel */
	}
	pthread_mutex_unlock(&s_task_node_pool_mtx);
}

struct task_manager* task_manager_init(const unsigned int size){
	struct task_manager* manager = (struct task_manager*)mem_malloc(sizeof(struct task_manager));
	if (!manager){
		LOGE(TASK_MANAGER_TAG, "manager malloc fail!");
		return NULL;
	}
	struct task_node** queue = (struct task_node**)mem_malloc(size * sizeof(struct task_node*));
	if (!queue){
		LOGE(TASK_MANAGER_TAG, "queue malloc fail!");
		mem_free(manager);
		return NULL;
	}

	for (int i = 0; i < size; ++i){
		queue[i] = NULL;
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
	node->dispatched = 0;
	strncpy(node->name, name, sizeof(node->name) - 1);
	node->name[sizeof(node->name) - 1] = '\0';
}

// Runtime-computed queue status (no stale flags)
bool task_manager_is_empty(const struct task_manager* mgr){
	/* Any non-NULL slot must be drained/cleaned before the worker can sleep. */
	for (int i = 0; i < mgr->size; ++i){
		if (mgr->queue[i] != NULL){
			return false;
		}
	}
	return true;
}

bool task_manager_is_full(const struct task_manager* mgr){
	for (int i = 0; i < mgr->size; ++i){
		struct task_node* node = mgr->queue[i];
		if (!node || node->cancel || node->done){
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
		struct task_node* node = worker_queue->queue[i];
		if (node &&
		    !node->cancel &&
		    !node->done &&
		    !strcmp(node->name, name)) {
			return node;
		}
	}
	return NULL;
}

// Priority sort on pointers only; task metadata is never copied.
void task_manager_pri_sort(struct task_manager* worker_queue){
	int size = worker_queue->size;
	if (size <= 1) return;

	// pri: first=1, middle=2, last=3
	int count[4] = {0};
	for (int i = 0; i < size; ++i){
		struct task_node* node = worker_queue->queue[i];
		if (!node || node->cancel || node->done) continue;
		int p = node->pri;
		if (p >= 1 && p <= 3) ++count[p];
	}

	int start[4] = {0, 0, count[1], count[1] + count[2]};

	// Pointer-only VLA: max 64 * 4B = 256B instead of copying task_node.
	struct task_node* temp[size];

	int pos[4] = {start[0], start[1], start[2], start[3]};
	for (int i = 0; i < size; ++i){
		struct task_node* node = worker_queue->queue[i];
		if (!node || node->cancel || node->done) continue;
		int p = node->pri;
		if (p < 1 || p > 3) continue;
		temp[pos[p]++] = node;
	}

	// Keep invalid-priority active nodes before the free/cancelled tail.
	int out = count[1] + count[2] + count[3];
	for (int i = 0; i < size; ++i){
		struct task_node* node = worker_queue->queue[i];
		if (node && !node->cancel && !node->done && (node->pri < first || node->pri > last)){
			temp[out++] = node;
		}
	}

	// Preserve free/cancelled slots at the tail.
	for (int i = 0; i < size; ++i){
		struct task_node* node = worker_queue->queue[i];
		if (!node || node->cancel || node->done){
			temp[out++] = node;
		}
	}

	memcpy(worker_queue->queue, temp, size * sizeof(struct task_node*));
}
