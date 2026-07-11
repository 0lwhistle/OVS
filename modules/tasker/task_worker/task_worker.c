#include "task_worker.h"

const char* TASK_WORKER_TAG = "[TASK_WORKER]";

struct task_worker_ctx s_task_worker_ctx = {0};



static inline uint64_t get_time_ms(void){
	return esp_timer_get_time() / 1000;
}

// forward declarations for static functions used before definition
static inline int enqueue_switcher(struct task_node* node);

int worker_init(void){

	int ret = 0;

	s_task_worker_ctx.little_worker = (struct task_worker*)malloc(sizeof(struct task_worker));
	s_task_worker_ctx.middle_worker = (struct task_worker*)malloc(sizeof(struct task_worker));
	s_task_worker_ctx.lots_worker = (struct task_worker*)malloc(sizeof(struct task_worker));
	s_task_worker_ctx.s_dispatcher = (struct task_worker*)malloc(sizeof(struct task_worker));
	s_task_worker_ctx.s_sched_table = (struct task_worker*)malloc(sizeof(struct task_worker));

	if (!s_task_worker_ctx.little_worker || !s_task_worker_ctx.middle_worker ||
		!s_task_worker_ctx.lots_worker || !s_task_worker_ctx.s_dispatcher ||
		!s_task_worker_ctx.s_sched_table) {
		ESP_LOGE(TASK_WORKER_TAG, "task worker malloc fail!");
		return TASK_MEM_ERR;
	}

	if(	worker_little_init() +
		worker_middle_init() +
		worker_lots_init() +
		worker_dispatcher_init() +
		worker_sched_init() != 0){
			ESP_LOGE(TASK_WORKER_TAG, "task worker init fail!");
			ret = TASK_INNER_ERR;
		}

	worker_init_flag = 1;

	return ret;

}


int worker_little_init(void){
	struct task_worker* worker = s_task_worker_ctx.little_worker;
	printf("little pthread init");
	int ret = TASK_OK;
	
	worker->worker_queue = task_manager_init(LITTLE_TASK_QUEUE_SIZE);
	if (!worker->worker_queue){
		printf("little worker task manager init fail.");
		ret = TASK_MEM_ERR;
		return ret;
	}

	ret = pthread_mutex_init(&(worker->mtx), NULL);
	if (ret != 0) {
		printf("Mutex init failed: %d\n", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	// 初始化条件变量（默认属性）
	ret = pthread_cond_init(&(worker->cond), NULL);
	if (ret != 0) {
		printf("Cond init failed: %d\n", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, LITTLE_TASK_STACK_SIZE);  // 设置栈大小
	ret = pthread_create(&(worker->pt), &attr, worker_little_handler, worker);
	pthread_attr_destroy(&attr);

	return ret;
}

int worker_middle_init(void){
	printf("middle pthread init");
	struct task_worker* worker = s_task_worker_ctx.middle_worker;

	int ret = TASK_OK;
	
	worker->worker_queue = task_manager_init(MIDDLE_TASK_QUEUE_SIZE);

	if (!worker->worker_queue){
		ret = TASK_MEM_ERR;
		return ret;
	}
	ret = pthread_mutex_init(&(worker->mtx), NULL);
	if (ret != 0) {
		printf("Mutex init failed: %d\n", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	// 初始化条件变量（默认属性）
	ret = pthread_cond_init(&(worker->cond), NULL);
	if (ret != 0) {
		printf("Cond init failed: %d\n", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, MIDDLE_TASK_STACK_SIZE);  // 设置栈大小
	ret = pthread_create(&(worker->pt), &attr, worker_middle_handler, worker);
	pthread_attr_destroy(&attr);


	return ret;

}

int worker_lots_init(void){
	printf("lots pthread init");
	struct task_worker* worker = s_task_worker_ctx.lots_worker;
	int ret = TASK_OK;
	
	worker->worker_queue = task_manager_init(LOTS_TASK_QUEUE_SIZE);

	if (!worker->worker_queue){
		ret = TASK_MEM_ERR;
		return ret;
	}
	ret = pthread_mutex_init(&(worker->mtx), NULL);
	if (ret != 0) {
		printf("Mutex init failed: %d\n", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	// 初始化条件变量（默认属性）
	ret = pthread_cond_init(&(worker->cond), NULL);
	if (ret != 0) {
		printf("Cond init failed: %d\n", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, LOTS_TASK_STACK_SIZE);  // 设置栈大小
	ret = pthread_create(&(worker->pt), &attr, worker_lots_handler, worker);
	pthread_attr_destroy(&attr);

	
	return ret;

}

int worker_dispatcher_init(void){

	printf("dispatcher pthread init");
	struct task_worker* worker = s_task_worker_ctx.s_dispatcher;
	int ret = TASK_OK;
	
	worker->worker_queue = task_manager_init(DISPATCHER_TASK_QUEUE_SIZE);
	if (!worker->worker_queue){
		ret = TASK_MEM_ERR;
		return ret;
	}
	ret = pthread_mutex_init(&(worker->mtx), NULL);
	if (ret != 0) {
		printf("Mutex init failed: %d\n", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	// 初始化条件变量（默认属性）
	ret = pthread_cond_init(&(worker->cond), NULL);
	if (ret != 0) {
		printf("Cond init failed: %d\n", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, DISPATCHER_TASK_QUEUE_STACK_SIZE);  // 设置栈大小
	ret = pthread_create(&(worker->pt), &attr, worker_dispatcher_handler, worker);
	pthread_attr_destroy(&attr);

	return ret;
}


int worker_sched_init(void){

	printf("dispatcher pthread init");
	struct task_worker* worker = s_task_worker_ctx.s_sched_table;
	int ret = TASK_OK;
	worker->worker_queue = task_manager_init(SCHED_TASK_QUEUE_SIZE);
	if (!worker->worker_queue){
		ret = TASK_MEM_ERR;
		return ret;
	}
	ret = pthread_mutex_init(&(worker->mtx), NULL);
	if (ret != 0) {
		printf("Mutex init failed: %d\n", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	// 初始化条件变量（默认属性）
	ret = pthread_cond_init(&(worker->cond), NULL);
	if (ret != 0) {
		printf("Cond init failed: %d\n", ret);
		ret = TASK_INNER_ERR;
		return ret;
	}

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, SCHED_TASK_QUEUE_STACK_SIZE);  // 设置栈大小
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
	int i;
	int size;
	int ret;
	int need_sort;
	worker->stop = 0;
	while(!worker->stop){
		pthread_mutex_lock(&(worker->mtx));
		while(!worker->stop && worker->worker_queue->is_empty)	{
			pthread_cond_wait(&worker->cond, &worker->mtx);
		}
		if (worker->stop) {
			pthread_mutex_unlock(&(worker->mtx));
			break;
		}

		i = 0;
		ret = 0;
		need_sort = 0;
		size = worker->worker_queue->size;
		struct task_manager* worker_queue = worker->worker_queue;
		for (; i < size; ++i){
			if (!worker_queue->queue[i].cancel && !worker_queue->queue[i].done){
					
				ret = enqueue_switcher(&(worker_queue->queue[i]));

				if (ret == TASK_STOP) {
					pthread_mutex_unlock(&(worker->mtx));
					return NULL;
				}

				if (ret == TASK_QUEUE_FULL) {
					need_sort = 1;
					task_node_pri_up(&(worker_queue->queue[i]));
					continue;
				}

				task_cancel(&(worker_queue->queue[i]));
				worker->worker_queue->is_full = 0;
			}
			
		}
		if (need_sort)
			task_manager_pri_sort(worker->worker_queue);

		pthread_mutex_unlock(&(worker->mtx));
		vTaskDelay(1);
	}
	pthread_mutex_unlock(&(worker->mtx));
	return NULL;
}

static inline struct task_node* find_dispatcher_vacancy(void){
	struct task_manager* worker_queue = s_task_worker_ctx.s_dispatcher->worker_queue;
	int size = s_task_worker_ctx.s_dispatcher->worker_queue->size;
	for(int i = 0; i < size; ++i){
		if (worker_queue->queue[i].cancel || worker_queue->queue[i].done){
			return &(worker_queue->queue[i]);
		}
	}

	//printf("%s dispatcher is full.", TASK_WORKER_TAG);
	s_task_worker_ctx.s_dispatcher->worker_queue->is_full = 1;
	s_task_worker_ctx.s_dispatcher->worker_queue->is_empty = 0;

	LOGW(TASK_WORKER_TAG, "dispatcher is full, can not find vacancy.");
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
			struct task_node* node = &worker_queue->queue[i];
			if (node->cancel || node->done) continue;

			int need_sched = 0;
			// 即时任务 (period == 0, run_cnt > 0)
			if (node->period == 0 && node->run_cnt > 0) {
				need_sched = 1;
			}
			// 延迟/周期任务 (period > 0, 时间到了)
			else if (node->period > 0 && node->run_cnt != 0 && 
				cur - node->inject_time >= node->period) {
				need_sched = 1;
			}

			if (!need_sched) continue;

			// 先更新状态，再 unlock 去 enqueue
			// 这样即使 enqueue 期间被其他线程修改，也不会重复调度
			uint64_t now = get_time_ms();
			node->inject_time = now;
			if (node->run_cnt > 0) --node->run_cnt;

			// 复制 node 到临时变量，然后 unlock 再 enqueue，避免死锁
			struct task_node tmp = *node;
			pthread_mutex_unlock(&(worker->mtx));

			ret = worker_task_enqueue_nocancel(s_task_worker_ctx.s_dispatcher, &tmp);

			// 重新 lock 继续遍历
			pthread_mutex_lock(&(worker->mtx));

			if (ret == TASK_QUEUE_FULL) {
				// enqueue 失败，恢复状态
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

	// 每个 worker 只创建一个 timer，后续复用
	worker_timer_init(worker);

	while(!worker->stop){
		pthread_mutex_lock(&(worker->mtx));
		while(!worker->stop && worker->worker_queue->is_empty)	{
			pthread_cond_wait(&worker->cond, &worker->mtx);
		}
		if (worker->stop) {
			pthread_mutex_unlock(&(worker->mtx));
			break;
		}

		int size = worker->worker_queue->size;
		struct task_manager* worker_queue = worker->worker_queue;
		for (int i = 0; i < size; ++i){
			if (worker_queue->queue[i].cancel || worker_queue->queue[i].done) continue;

			// 复制任务到临时变量，然后 unlock 执行，避免长时间持有锁阻塞 dispatcher
			struct task_node task_copy = worker_queue->queue[i];
			worker->timeout_flag = 0;

			pthread_mutex_unlock(&(worker->mtx));

			// 如果任务有超时设置，启动 timer
			if (task_copy.timeout > 0) {
				ESP_ERROR_CHECK(esp_timer_start_once(worker->timeout_timer, task_copy.timeout * 1000));
			}
			status = task_copy.fn(task_copy.ctx);

			// 停止 timer（如果还在跑）
			if (task_copy.timeout > 0) {
				esp_timer_stop(worker->timeout_timer);
			}

			// 重新 lock 更新状态
			pthread_mutex_lock(&(worker->mtx));

			// 重新获取节点指针（可能在 unlock 期间被修改）
			struct task_node* node = &worker_queue->queue[i];
			if (node->cancel || node->done) continue;

			if (status == TASK_OK){
				task_done(node);
				node->is_timeout = worker->timeout_flag;
				if (worker->timeout_flag && node->level < level_middle){
					struct task_node* tmp = find_task_node_by_name(s_task_worker_ctx.s_sched_table->worker_queue, node->name);
					if (tmp){
						task_node_leve_up(tmp);
					}
				}
			} else {
				LOGW(TASK_WORKER_TAG, "task node not done, cancel it.");
				task_cancel(node);
			}

			worker->worker_queue->is_full = 0;
		}
		// 遍历结束后检查队列是否真的为空
		{
			int all_done = 1;
			for (int j = 0; j < size; ++j) {
				if (!worker_queue->queue[j].cancel && !worker_queue->queue[j].done) {
					all_done = 0;
					break;
				}
			}
			worker_queue->is_empty = all_done;
		}
		pthread_mutex_unlock(&(worker->mtx));
	}
	// 退出时清理 timer
	esp_timer_delete(worker->timeout_timer);
	pthread_mutex_unlock(&(worker->mtx));
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
		if (worker_queue->queue[i].cancel || worker_queue->queue[i].done){

			worker_queue->queue[i].cancel = node->cancel;
			worker_queue->queue[i].ctx = node->ctx;
			worker_queue->queue[i].done = node->done;
			worker_queue->queue[i].fn = node->fn;
			worker_queue->queue[i].pri = node->pri;
			worker_queue->queue[i].level = node->level;
			worker_queue->queue[i].timeout = node->timeout;
			worker_queue->queue[i].is_timeout = node->is_timeout;
			strncpy(worker_queue->queue[i].name, node->name, sizeof(worker_queue->queue[i].name) - 1);
			worker_queue->queue[i].name[sizeof(worker_queue->queue[i].name) - 1] = '\0';
			worker_queue->queue[i].inject_time = node->inject_time;
			worker_queue->queue[i].run_cnt = node->run_cnt;
			worker_queue->queue[i].period = node->period;
			LOGI(TASK_WORKER_TAG, "task: %s enqueue successfully.", worker_queue->queue[i].name);

			worker_queue->is_empty = 0;

			pthread_cond_signal(&(des->cond));
			pthread_mutex_unlock(&(des->mtx));
			task_cancel(node);
			return TASK_OK;
		}
	}

	worker_queue->is_empty = 0;
	worker_queue->is_full = 1;
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
		if (worker_queue->queue[i].cancel || worker_queue->queue[i].done){

			worker_queue->queue[i].cancel = node->cancel;
			worker_queue->queue[i].ctx = node->ctx;
			worker_queue->queue[i].done = node->done;
			worker_queue->queue[i].fn = node->fn;
			worker_queue->queue[i].pri = node->pri;
			worker_queue->queue[i].level = node->level;
			worker_queue->queue[i].timeout = node->timeout;
			worker_queue->queue[i].is_timeout = node->is_timeout;
			strncpy(worker_queue->queue[i].name, node->name, sizeof(worker_queue->queue[i].name) - 1);
			worker_queue->queue[i].name[sizeof(worker_queue->queue[i].name) - 1] = '\0';
			worker_queue->queue[i].inject_time = node->inject_time;
			worker_queue->queue[i].run_cnt = node->run_cnt;
			worker_queue->queue[i].period = node->period;
			LOGI(TASK_WORKER_TAG, "task: %s enqueue successfully.", worker_queue->queue[i].name);

			worker_queue->is_empty = 0;

			pthread_cond_signal(&(des->cond));
			pthread_mutex_unlock(&(des->mtx));
			return TASK_OK;
		}
	}

	worker_queue->is_empty = 0;
	worker_queue->is_full = 1;
	pthread_mutex_unlock(&(des->mtx));

	return TASK_QUEUE_FULL;
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
			printf("unknow level");
			return TASK_INNER_ERR;
	}
}

static inline void task_expire(struct task_node* des, struct task_node* src){
	pthread_mutex_lock(&(s_task_worker_ctx.s_dispatcher->mtx));

	des->cancel = src->cancel;
	des->ctx = src->ctx;
	des->done = src->done;
	des->fn = src->fn;
	des->is_timeout = src->is_timeout;
	des->level = src->level;
	strncpy(des->name, src->name, sizeof(des->name) - 1);
	des->name[sizeof(des->name) - 1] = '\0';
	des->pri = src->pri;
	des->timeout = src->timeout;
	des->inject_time = src->inject_time;
	if (src->period == 0)
		des->run_cnt = src->run_cnt;
	else
		des->period = src->period;
	


	task_cancel(src);

	pthread_cond_signal(&(s_task_worker_ctx.s_dispatcher->cond));
	pthread_mutex_unlock(&(s_task_worker_ctx.s_dispatcher->mtx));
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
	free(worker->worker_queue->queue);
	free(worker->worker_queue);
	free(worker);

}

// API
int shched_enqueue(struct task_node* node){
	if (!node) {
		LOGW(TASK_WORKER_TAG, "shched_enqueue fail, node is null");
		return TASK_PARA_ERR;
	}

	if (node->cancel || node->done || node->period < 0 || !node->fn || !strlen(node->name)){
		LOGW(TASK_WORKER_TAG, "shched_enqueue fail, node is invaild");
		return TASK_PARA_ERR;
	}

	if (!worker_init_flag){
		int ret = worker_init();
		if (ret != 0) {
			printf("worker_init failed: %d\n", ret);
			return TASK_INNER_ERR;
		}
	}

	return worker_task_enqueue(s_task_worker_ctx.s_sched_table, node);
}

void shched_cancel_by_node(struct task_node* node){
	shched_cancel_by_name(node->name);
	return;
}

void shched_cancel_by_name(const char* name){
	pthread_mutex_lock(&(s_task_worker_ctx.s_sched_table->mtx));
	struct task_node* node = find_task_node_by_name(s_task_worker_ctx.s_sched_table->worker_queue, name);
	if (node) task_cancel(node);
	pthread_mutex_unlock(&(s_task_worker_ctx.s_sched_table->mtx));
	return;
}

int shched_is_full(void){
	return s_task_worker_ctx.s_sched_table->worker_queue->is_full;
}

int shched_is_empty(void){
	return s_task_worker_ctx.s_sched_table->worker_queue->is_empty;
}

struct task_node* sched_task_init_li( 
									const int period, 
									const int run_cnt, 
									const char* name, 
									task_fn fn, void* ctx
								){

	if (period < 0 || !fn || !name || !strlen(name)){
		LOGW(TASK_WORKER_TAG, "shched_enqueue fail, node is invaild");
		return NULL;
	}

	if (!worker_init_flag){
		int ret = worker_init();
		if (ret != 0) {
			printf("worker_init failed: %d\n", ret);
			return NULL;
		}
	}
	
	return task_init(LITTLE_TASK_DEFAULT_TIMEOUT, get_time_ms(), period, run_cnt, last, level_little, name, fn, ctx);
}

struct task_node* sched_task_init_mi(
									const int period, 
									const int run_cnt, 
									const char* name, 
									task_fn fn, void* ctx
								){

	if (period < 0 || !fn || !name || !strlen(name)){
		LOGW(TASK_WORKER_TAG, "shched_enqueue fail, node is invaild");
		return NULL;
	}

	if (!worker_init_flag){
		int ret = worker_init();
		if (ret != 0) {
			printf("worker_init failed: %d\n", ret);
			return NULL;
		}
	}

	return task_init(MIDDLE_TASK_DEFAULT_TIMEOUT, get_time_ms(), period, run_cnt, last, level_middle, name, fn, ctx);
}

struct task_node* sched_task_init_lo(const int timeout, 
									const int period, 
									const int run_cnt, 
									const char* name, 
									task_fn fn, void* ctx
								){

	if (period < 0 || !fn || !name || !strlen(name)){
		LOGW(TASK_WORKER_TAG, "shched_enqueue fail, node is invaild");
		return NULL;
	}	
	
	if (!worker_init_flag){
		int ret = worker_init();
		if (ret != 0) {
			printf("worker_init failed: %d\n", ret);
			return NULL;
		}
	}

	return task_init(timeout, get_time_ms(), period, run_cnt, last, level_lots, name, fn, ctx);
}
