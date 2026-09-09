#include "task_worker.h"
#include "mem.h"

const char* TASK_WORKER_TAG = "[TASK_WORKER]";

int worker_init_flag = 0;

struct task_worker_ctx s_task_worker_ctx = {0};



static inline uint64_t get_time_ms(void){
	return esp_timer_get_time() / 1000;
}

// forward declarations for static functions used before definition
static inline int enqueue_switcher(struct task_node* node);
static int worker_task_enqueue_nocancel(struct task_worker* des, struct task_node* node);

int worker_init(void){

	int ret = 0;

	s_task_worker_ctx.little_worker = (struct task_worker*)mem_malloc(sizeof(struct task_worker));
	s_task_worker_ctx.middle_worker = (struct task_worker*)mem_malloc(sizeof(struct task_worker));
	s_task_worker_ctx.lots_worker = (struct task_worker*)mem_malloc(sizeof(struct task_worker));
	s_task_worker_ctx.s_dispatcher = (struct task_worker*)mem_malloc(sizeof(struct task_worker));
	s_task_worker_ctx.s_sched_table = (struct task_worker*)mem_malloc(sizeof(struct task_worker));

	if (!s_task_worker_ctx.little_worker || !s_task_worker_ctx.middle_worker ||
		!s_task_worker_ctx.lots_worker || !s_task_worker_ctx.s_dispatcher ||
		!s_task_worker_ctx.s_sched_table) {
		ESP_LOGE(TASK_WORKER_TAG, "task worker malloc fail!");
		return TASK_MEM_ERR;
	}

	if (worker_little_init() != TASK_OK ||
	    worker_middle_init() != TASK_OK ||
	    worker_lots_init() != TASK_OK ||
	    worker_dispatcher_init() != TASK_OK ||
	    worker_sched_init() != TASK_OK) {
		ESP_LOGE(TASK_WORKER_TAG, "task worker init fail!");
		ret = TASK_INNER_ERR;
	}

	worker_init_flag = 1;

	return ret;

}


int worker_little_init(void){
	struct task_worker* worker = s_task_worker_ctx.little_worker;
	LOGI(TASK_WORKER_TAG, "little worker pthread init");
	int ret = TASK_OK;
	
	worker->worker_queue = task_manager_init(LITTLE_TASK_QUEUE_SIZE);
	if (!worker->worker_queue){
		LOGE(TASK_WORKER_TAG, "little worker task manager init fail");
		ret = TASK_MEM_ERR;
		return ret;
	}

	ret = pthread_mutex_init(&(worker->mtx), NULL);
	if (ret != 0) {
		ESP_LOGE(TASK_WORKER_TAG, "Mutex init failed: %d", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	ret = pthread_cond_init(&(worker->cond), NULL);
	if (ret != 0) {
		ESP_LOGE(TASK_WORKER_TAG, "Cond init failed: %d", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, LITTLE_TASK_STACK_SIZE);
	ret = pthread_create(&(worker->pt), &attr, worker_little_handler, worker);
	pthread_attr_destroy(&attr);

	return ret;
}

int worker_middle_init(void){
	LOGI(TASK_WORKER_TAG, "middle worker pthread init");
	struct task_worker* worker = s_task_worker_ctx.middle_worker;

	int ret = TASK_OK;
	
	worker->worker_queue = task_manager_init(MIDDLE_TASK_QUEUE_SIZE);

	if (!worker->worker_queue){
		ret = TASK_MEM_ERR;
		return ret;
	}
	ret = pthread_mutex_init(&(worker->mtx), NULL);
	if (ret != 0) {
		ESP_LOGE(TASK_WORKER_TAG, "Mutex init failed: %d", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	ret = pthread_cond_init(&(worker->cond), NULL);
	if (ret != 0) {
		ESP_LOGE(TASK_WORKER_TAG, "Cond init failed: %d", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, MIDDLE_TASK_STACK_SIZE);
	ret = pthread_create(&(worker->pt), &attr, worker_middle_handler, worker);
	pthread_attr_destroy(&attr);


	return ret;

}

int worker_lots_init(void){
	LOGI(TASK_WORKER_TAG, "lots worker pthread init");
	struct task_worker* worker = s_task_worker_ctx.lots_worker;
	int ret = TASK_OK;
	
	worker->worker_queue = task_manager_init(LOTS_TASK_QUEUE_SIZE);

	if (!worker->worker_queue){
		ret = TASK_MEM_ERR;
		return ret;
	}
	ret = pthread_mutex_init(&(worker->mtx), NULL);
	if (ret != 0) {
		ESP_LOGE(TASK_WORKER_TAG, "Mutex init failed: %d", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	ret = pthread_cond_init(&(worker->cond), NULL);
	if (ret != 0) {
		ESP_LOGE(TASK_WORKER_TAG, "Cond init failed: %d", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, LOTS_TASK_STACK_SIZE);
	ret = pthread_create(&(worker->pt), &attr, worker_lots_handler, worker);
	pthread_attr_destroy(&attr);

	
	return ret;

}

int worker_dispatcher_init(void){

	LOGI(TASK_WORKER_TAG, "dispatcher worker pthread init");
	struct task_worker* worker = s_task_worker_ctx.s_dispatcher;
	int ret = TASK_OK;
	
	worker->worker_queue = task_manager_init(DISPATCHER_TASK_QUEUE_SIZE);
	if (!worker->worker_queue){
		ret = TASK_MEM_ERR;
		return ret;
	}
	ret = pthread_mutex_init(&(worker->mtx), NULL);
	if (ret != 0) {
		ESP_LOGE(TASK_WORKER_TAG, "Mutex init failed: %d", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	ret = pthread_cond_init(&(worker->cond), NULL);
	if (ret != 0) {
		ESP_LOGE(TASK_WORKER_TAG, "Cond init failed: %d", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, DISPATCHER_TASK_QUEUE_STACK_SIZE);
	ret = pthread_create(&(worker->pt), &attr, worker_dispatcher_handler, worker);
	pthread_attr_destroy(&attr);

	return ret;
}


int worker_sched_init(void){

	LOGI(TASK_WORKER_TAG, "sched worker pthread init");
	struct task_worker* worker = s_task_worker_ctx.s_sched_table;
	int ret = TASK_OK;
	worker->worker_queue = task_manager_init(SCHED_TASK_QUEUE_SIZE);
	if (!worker->worker_queue){
		ret = TASK_MEM_ERR;
		return ret;
	}
	ret = pthread_mutex_init(&(worker->mtx), NULL);
	if (ret != 0) {
		ESP_LOGE(TASK_WORKER_TAG, "Mutex init failed: %d", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	ret = pthread_cond_init(&(worker->cond), NULL);
	if (ret != 0) {
		ESP_LOGE(TASK_WORKER_TAG, "Cond init failed: %d", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, SCHED_TASK_QUEUE_STACK_SIZE);
	ret = pthread_create(&(worker->pt), &attr, worker_sched_handler, worker);
	pthread_attr_destroy(&attr);

	return ret;
}


void* worker_little_handler(void* arg){
	struct task_worker* worker = (struct task_worker*)arg;
	worker_do_handler(worker);
	return NULL;
}

void* worker_middle_handler(void* arg){
	struct task_worker* worker = (struct task_worker*)arg;
	worker_do_handler(worker);
	return NULL;
}

void* worker_lots_handler(void* arg){
	struct task_worker* worker = (struct task_worker*)arg;
	worker_do_handler(worker);
	return NULL;
}

void* worker_dispatcher_handler(void* arg){
	struct task_worker* worker = (struct task_worker*)arg;
	int size;
	int ret;
	int need_sort;
	worker->stop = 0;
	while(!worker->stop){
		pthread_mutex_lock(&(worker->mtx));
		while(!worker->stop && task_manager_is_empty(worker->worker_queue))	{
			pthread_cond_wait(&worker->cond, &worker->mtx);
		}
		if (worker->stop) {
			pthread_mutex_unlock(&(worker->mtx));
			break;
		}

		need_sort = 0;
		size = worker->worker_queue->size;
		struct task_manager* worker_queue = worker->worker_queue;
		for (int i = 0; i < size; ++i){
			struct task_node* node = worker_queue->queue[i];
			if (!node) continue;

			if (node->cancel || node->done){
				worker_queue->queue[i] = NULL;
				node->done = 1;
				node->dispatched = 0;
				continue;
			}

			ret = enqueue_switcher(node);

			if (ret == TASK_STOP) {
				pthread_mutex_unlock(&(worker->mtx));
				return NULL;
			}

			if (ret == TASK_QUEUE_FULL) {
				need_sort = 1;
				task_node_pri_up(node);
				continue;
			}

			/* Pointer handoff: dispatcher slot no longer references the node. */
			worker_queue->queue[i] = NULL;
		}
		if (need_sort)
			task_manager_pri_sort(worker->worker_queue);

		pthread_mutex_unlock(&(worker->mtx));
		vTaskDelay(1);

	}
	return NULL;
}
void* worker_sched_handler(void* arg){
	struct task_worker* worker = (struct task_worker*)arg;
	int ret = 0;
	worker->stop = 0;
	while(!worker->stop){
		vTaskDelay(1);

		pthread_mutex_lock(&(worker->mtx));

		int size = worker->worker_queue->size;
		struct task_manager* worker_queue = worker->worker_queue;
		uint64_t cur = get_time_ms();
		for (int i = 0; i < size; ++i){
			struct task_node* node = worker_queue->queue[i];
			if (!node) continue;

			if (node->cancel || node->done){
				if (!node->dispatched){
					task_node_pool_free(node);
					worker_queue->queue[i] = NULL;
				}
				continue;
			}

			/* Node is currently borrowed by dispatcher/worker; wait for it to return. */
			if (node->dispatched) continue;

			int need_sched = 0;
			if (node->period == 0 && node->run_cnt > 0) {
				need_sched = 1;
			}
			else if (node->period > 0 && node->run_cnt != 0 &&
				cur - node->inject_time >= node->period) {
				need_sched = 1;
			}

			if (!need_sched) continue;

			uint64_t now = get_time_ms();
			node->inject_time = now;
			if (node->run_cnt > 0) --node->run_cnt;

			/* Move the node pointer to dispatcher without copying task metadata. */
			node->dispatched = 1;
			ret = worker_task_enqueue_nocancel(s_task_worker_ctx.s_dispatcher, node);
			if (ret == TASK_OK) continue;

			node->dispatched = 0;
			if (ret == TASK_QUEUE_FULL) {
				node->inject_time = cur;
				if (node->run_cnt >= 0) ++node->run_cnt;
				task_node_pri_up(node);
			}
		}

		pthread_mutex_unlock(&(worker->mtx));
	}
	return NULL;

}
static inline void timer_callback(void* arg){
	struct task_worker* worker = (struct task_worker*)arg;
	worker->timeout_flag = 1;
	return;
}

static inline void worker_timer_init(struct task_worker* worker){
	const esp_timer_create_args_t timer_args = {
		.callback = &timer_callback,
		.arg = worker,
		.name = "worker_timeout"
	};
	ESP_ERROR_CHECK(esp_timer_create(&timer_args, &worker->timeout_timer));
}

void worker_do_handler(struct task_worker* worker){

	enum task_t status;
	worker->stop = 0;
	worker->timeout_flag = 0;
	worker->timeout_timer = NULL;

	worker_timer_init(worker);

	while(!worker->stop){
		pthread_mutex_lock(&(worker->mtx));
		while(!worker->stop && task_manager_is_empty(worker->worker_queue))	{
			pthread_cond_wait(&worker->cond, &worker->mtx);
		}
		if (worker->stop) {
			pthread_mutex_unlock(&(worker->mtx));
			break;
		}

		int size = worker->worker_queue->size;
		struct task_manager* worker_queue = worker->worker_queue;
		for (int i = 0; i < size; ++i){
			struct task_node* node = worker_queue->queue[i];
			if (!node) continue;

			if (node->cancel || node->done){
				worker_queue->queue[i] = NULL;
				if (node->dispatched){
					node->done = 1;
					node->dispatched = 0;
				}
				continue;
			}

			task_fn fn = node->fn;
			void* ctx = node->ctx;
			int timeout = node->timeout;
			worker->timeout_flag = 0;

			pthread_mutex_unlock(&(worker->mtx));

			if (timeout > 0) {
				ESP_ERROR_CHECK(esp_timer_start_once(worker->timeout_timer, timeout * 1000));
			}
			status = fn(ctx);

			if (timeout > 0) {
				esp_timer_stop(worker->timeout_timer);
			}

			pthread_mutex_lock(&(worker->mtx));

			/* The node remains in sched table; only the worker slot is cleared here. */
			node = worker_queue->queue[i];
			if (!node) continue;

			if (node->cancel || node->done){
				worker_queue->queue[i] = NULL;
				if (node->dispatched){
					node->done = 1;
					node->dispatched = 0;
				}
				continue;
			}

			if (status == TASK_OK){
				node->is_timeout = worker->timeout_flag;
				if (worker->timeout_flag && node->level < level_lots){
					task_node_leve_up(node);
				}
			} else {
				LOGW(TASK_WORKER_TAG, "task node returned error, schedule will retry it.");
			}

			worker_queue->queue[i] = NULL;
			node->dispatched = 0;
			if (node->run_cnt == 0 || node->cancel || node->done){
				node->done = 1;
			}
		}
		pthread_mutex_unlock(&(worker->mtx));
	}
	esp_timer_delete(worker->timeout_timer);
}
int worker_task_enqueue(struct task_worker* des, struct task_node* node){
	if (des->stop){
		ESP_LOGW(TASK_WORKER_TAG, "worker is already stop.");
		return TASK_STOP;
	}

	pthread_mutex_lock(&(des->mtx));

	int size = des->worker_queue->size;
	struct task_manager* worker_queue = des->worker_queue;
	for (int i = 0; i < size; ++i){
		struct task_node* slot = worker_queue->queue[i];
		if (slot == NULL){

			worker_queue->queue[i] = node;
			LOGD(TASK_WORKER_TAG, "task: %s enqueue successfully.", node->name);

			pthread_cond_signal(&(des->cond));
			pthread_mutex_unlock(&(des->mtx));
			return TASK_OK;
		}

		if ((slot->cancel || slot->done) && !slot->dispatched){
			task_node_pool_free(slot);
			worker_queue->queue[i] = node;
			LOGD(TASK_WORKER_TAG, "task: %s enqueue successfully.", node->name);

			pthread_cond_signal(&(des->cond));
			pthread_mutex_unlock(&(des->mtx));
			return TASK_OK;
		}
	}

	pthread_mutex_unlock(&(des->mtx));
	task_node_pri_up(node);

	return TASK_QUEUE_FULL;
}
static int worker_task_enqueue_nocancel(struct task_worker* des, struct task_node* node){
	if (des->stop){
		ESP_LOGW(TASK_WORKER_TAG, "worker is already stop.");
		return TASK_STOP;
	}

	pthread_mutex_lock(&(des->mtx));

	int size = des->worker_queue->size;
	struct task_manager* worker_queue = des->worker_queue;
	for (int i = 0; i < size; ++i){
		struct task_node* slot = worker_queue->queue[i];
		if (slot == NULL){

			worker_queue->queue[i] = node;
			LOGD(TASK_WORKER_TAG, "task: %s enqueue successfully.", node->name);

			pthread_cond_signal(&(des->cond));
			pthread_mutex_unlock(&(des->mtx));
			return TASK_OK;
		}

		if ((slot->cancel || slot->done) && !slot->dispatched){
			task_node_pool_free(slot);
			worker_queue->queue[i] = node;
			LOGD(TASK_WORKER_TAG, "task: %s enqueue successfully.", node->name);

			pthread_cond_signal(&(des->cond));
			pthread_mutex_unlock(&(des->mtx));
			return TASK_OK;
		}
	}

	pthread_mutex_unlock(&(des->mtx));

	return TASK_QUEUE_FULL;
}

int worker_sched_enqueue(struct task_node* node){
	if (!node) return TASK_PARA_ERR;

	struct task_worker* des = s_task_worker_ctx.s_sched_table;
	if (des->stop){
		ESP_LOGW(TASK_WORKER_TAG, "worker is already stop.");
		return TASK_STOP;
	}

	pthread_mutex_lock(&(des->mtx));

	int size = des->worker_queue->size;
	struct task_manager* worker_queue = des->worker_queue;
	int slot = -1;
	for (int i = 0; i < size; ++i){
		struct task_node* cur = worker_queue->queue[i];
		if (cur == NULL){
			slot = i;
			break;
		}
		if ((cur->cancel || cur->done) && !cur->dispatched){
			task_node_pool_free(cur);
			slot = i;
			break;
		}
	}

	if (slot < 0){
		pthread_mutex_unlock(&(des->mtx));
		return TASK_QUEUE_FULL;
	}

	/* Reclaim first, then take a pool node, so a full stale sched table can still admit tasks. */
	struct task_node* owned = task_node_pool_alloc();
	if (!owned){
		pthread_mutex_unlock(&(des->mtx));
		return TASK_QUEUE_FULL;
	}
	*owned = *node;
	owned->cancel = 0;
	owned->done = 0;
	owned->dispatched = 0;

	worker_queue->queue[slot] = owned;
	LOGI(TASK_WORKER_TAG, "task: %s enqueue successfully.", owned->name);

	pthread_cond_signal(&(des->cond));
	pthread_mutex_unlock(&(des->mtx));
	return TASK_OK;
}

static inline int enqueue_switcher(struct task_node* node){
	switch (node->level){
		case level_little:
			return worker_task_enqueue(s_task_worker_ctx.little_worker, node);
		
		case level_middle:
			return worker_task_enqueue(s_task_worker_ctx.middle_worker, node);

		case level_lots:
			return worker_task_enqueue(s_task_worker_ctx.lots_worker, node);

		default:
			ESP_LOGE(TASK_WORKER_TAG, "unknown level in enqueue_switcher");
			return TASK_INNER_ERR;
	}
}


void worker_task_done(struct task_worker* worker, struct task_node* node){
	pthread_mutex_lock(&(worker->mtx));
	struct task_node* des = find_task_node_by_name(worker->worker_queue, node->name);
	if (des) des->done = 1;
	pthread_mutex_unlock(&(worker->mtx));
}

void worker_task_cancel(struct task_worker* worker, struct task_node* node){
	pthread_mutex_lock(&(worker->mtx));
	struct task_node* des = find_task_node_by_name(worker->worker_queue, node->name);
	if (des) des->cancel = 1;
	pthread_mutex_unlock(&(worker->mtx));
}



void worker_delete(struct task_worker* worker){
	worker->stop = 1;
	pthread_cond_signal(&(worker->cond));
	pthread_join(worker->pt, NULL);
	pthread_mutex_destroy(&(worker->mtx));	
	pthread_cond_destroy(&(worker->cond));
	mem_free(worker->worker_queue->queue);
	mem_free(worker->worker_queue);
	mem_free(worker);

}
