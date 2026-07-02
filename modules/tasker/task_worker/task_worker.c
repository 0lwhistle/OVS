#include "task_worker.h"
#include <sched.h>

struct task_worker_ctx s_task_worker_ctx = {0};

static inline void worker_bind_cpu(pthread_t pt, int cpu_id){
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(cpu_id, &cpuset);
	pthread_setaffinity_np(pt, sizeof(cpu_set_t), &cpuset);
}

int worker_init(void){

	int ret = 0;

	s_task_worker_ctx.little_worker = (struct task_worker*)malloc(sizeof(struct task_worker));
	s_task_worker_ctx.middle_worker = (struct task_worker*)malloc(sizeof(struct task_worker));
	s_task_worker_ctx.lots_worker = (struct task_worker*)malloc(sizeof(struct task_worker));
	s_task_worker_ctx.s_dispatcher = (struct task_worker*)malloc(sizeof(struct task_worker));
	s_task_worker_ctx.s_sched_table = (struct task_worker*)malloc(sizeof(struct task_worker));



	if(	worker_little_init() +
		worker_middle_init() +
		worker_lots_init() +
		worker_dispatcher_init() +
		worker_sched_init() != 0){
			ESP_LOGE(TASK_WORKER_TAG, "task worker init fail!");
			ret = TASK_INNER_ERR;
		}

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
	worker_bind_cpu(worker->pt, 1);

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
	worker_bind_cpu(worker->pt, 1);


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
	worker_bind_cpu(worker->pt, 1);

	
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
	worker_bind_cpu(worker->pt, 0);

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
	worker_bind_cpu(worker->pt, 0);

	return ret;
}


void worker_little_handler(struct task_worker* worker){	
	worker_do_handler(worker);
}

void worker_middle_handler(struct task_worker* worker){
	worker_do_handler(worker);
}

void worker_lots_handler(struct task_worker* worker){
	worker_do_handler(worker);
}

void worker_dispatcher_handler(struct task_worker* worker){
	int i;
	int size;
	int ret;
	int need_sort;
	enum task_t status;
	worker->stop = 0;
	while(!worker->stop){
		pthread_mutex_lock(&(worker->mtx));
		while(!worker->stop && worker->worker_queue->is_empty)	{
			pthread_cond_wait(&worker->cond, &worker->mtx);
		}
		
		i = 0;
		ret = 0;
		need_sort = 0;
		size = worker->worker_queue->size;
		struct task_manager* worker_queue = worker->worker_queue;
		for (; i < size; ++i){
			if (!worker_queue->queue[i].cancel && !worker_queue->queue[i].done){
				// // Have to use deep copy to dispatch the task, avoiding the memory error
				// struct task_node* node = task_init(worker_queue->queue[i].timeout,
				// 	worker_queue->queue[i].inject_time,
				// 	worker_queue->queue[i].period,
				// 	worker_queue->queue[i].run_cnt,
				// 	worker_queue->queue[i].pri,
				// 	worker_queue->queue[i].level,
				// 	worker_queue->queue[i].name,
				// 	worker_queue->queue[i].fn,
				// 	worker_queue->queue[i].ctx
				// );
					
				ret = enqueue_switcher(&(worker_queue->queue[i]));

				if (ret == TASK_STOP) break;

				if (ret == TASK_QUEUE_FULL) {
					need_sort = 1;
					task_node_pri_up(&(worker_queue->queue[i]));
					continue;
				}

				task_cancel(&(worker_queue->queue[i]));
				s_task_worker_ctx.s_dispatcher->worker_queue->is_full = 0;
			}
			
		}
		if (need_sort)
			task_manager_pri_sort(worker->worker_queue);

		pthread_mutex_unlock(&(worker->mtx));
	}
	pthread_mutex_unlock(&(worker->mtx));
}

static inline uint64_t get_time_ms(void){
	return esp_timer_get_time() / 1000;
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

void worker_sched_handler(struct task_worker* worker){
	int i = 0;
	int size = 0;
	worker->stop = 0;
	while(!worker->stop){
		pthread_mutex_lock(&(worker->mtx));
		while(!worker->stop && worker->worker_queue->is_empty)	{
			pthread_cond_wait(&worker->cond, &worker->mtx);
		}
		
		if (s_task_worker_ctx.s_dispatcher->worker_queue->is_full) {
			LOGW(TASK_WORKER_TAG, "dispatcher is full now");
			continue;
		}

		i = 0;
		size = worker->worker_queue->size;
		struct task_manager* worker_queue = worker->worker_queue;
		uint64_t cur = get_time_ms();
		for (; i < size; ++i){
			if (!worker_queue->queue[i].cancel && !worker_queue->queue[i].done){
				if ((worker_queue->queue[i].period == 1 || worker_queue->queue[i].run_cnt > 0)&& 
					cur - worker_queue->queue[i].inject_time >= worker_queue->queue[i].period){
					worker_task_enqueue(s_task_worker_ctx.s_dispatcher, &(worker_queue->queue[i]));
					worker_queue->queue[i].inject_time = get_time_ms();
					if (worker_queue->queue[i].period == 0) --worker_queue->queue[i].run_cnt;
				} else {
					worker_task_cancel(worker, &(worker->worker_queue->queue[i]));
				}
			}
			
		}
		pthread_mutex_unlock(&(worker->mtx));
	}
	pthread_mutex_unlock(&(worker->mtx));

}

void timer_callback(void* arg){
	int* timeout = (int*)arg;

	*timeout = 1;
	return;
}

esp_timer_handle_t* timeout_timer_init(const int timeout, void* arg){

	if (!arg){
		printf("%s: timer arg is null!", TASK_WORKER_TAG);
		// ESP_LOGE(TASK_WORKER_TASK_WORKER_TAG, "timer arg is null!");
		return NULL;
	}

	// 创建定时器时传入参数
	const esp_timer_create_args_t oneshot_args = {
		.callback = &timer_callback,
		.arg = arg,  // 关键：传递参数指针
		.name = "worker_handler_timer"
	};
	
	esp_timer_handle_t* oneshot_timer = (esp_timer_handle_t*)malloc(sizeof(esp_timer_handle_t));
	ESP_ERROR_CHECK(esp_timer_create(&oneshot_args, oneshot_timer));
	ESP_ERROR_CHECK(esp_timer_start_once(*oneshot_timer, timeout * 1000));

	return oneshot_timer;
}



void worker_do_handler(struct task_worker* worker){

	int i = 0;
	int size = 0;
	int timeout_flag = 0;
	enum task_t status;
	worker->stop = 0;
	while(!worker->stop){
		pthread_mutex_lock(&(worker->mtx));
		while(!worker->stop && worker->worker_queue->is_empty)	{
			pthread_cond_wait(&worker->cond, &worker->mtx);
		}
		
		i = 0;
		size = worker->worker_queue->size;
		struct task_manager* worker_queue = worker->worker_queue;
		worker_queue->is_empty = 1;
		for (; i < size; ++i){
			timeout_flag = 0;
			if (!worker_queue->queue[i].cancel && !worker_queue->queue[i].done){
				worker_queue->is_empty = 0;
				// start a oneshot timer, than run the task
				esp_timer_handle_t* timer = timeout_timer_init(worker_queue->queue[i].timeout, &timeout_flag);
				status = worker_queue->queue[i].fn(worker_queue->queue[i].ctx);

				// delete the oneshot timer after task
				ESP_ERROR_CHECK(esp_timer_delete(timer));
				if (status == TASK_OK){
					// set the done flag
					task_done(&(worker_queue->queue[i]));

					// log the timeout case
					worker_queue->queue[i].is_timeout = timeout_flag;
					if (timeout_flag && worker_queue->queue->level < 2){
						++worker_queue->queue->level;
					}
				} else {
					LOGW(TASK_WORKER_TAG, "task node not done, cancel it.");
					task_cancel(&(worker_queue->queue[i]));
				}

				worker->worker_queue->is_full = 0;
			}
		}
		pthread_mutex_unlock(&(worker->mtx));
	}
	pthread_mutex_lock(&(worker->mtx));
}


int worker_task_enqueue(struct task_worker* des, struct task_node* node){
	if (des->stop){
		ESP_LOGW(TASK_WORKER_TAG, "worker is already stop.");
		return TASK_STOP;
	}
	if(des->worker_queue->is_full){
		ESP_LOGW(TASK_WORKER_TAG, "worker is full now.");
		task_node_pri_up(node);
		return TASK_QUEUE_FULL;
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
			worker_queue->queue[i].name = node->name;
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

	return TASK_QUEUE_FULL;
}

static inline int enqueue_switcher(struct task_node* node){
	switch (node->level){
		case little:
			return worker_task_enqueue(s_task_worker_ctx.little_worker, node);
		
		case middle:
			return worker_task_enqueue(s_task_worker_ctx.middle_worker, node);

		case lots:
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
	des->name = src->name;
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
	des->done = 1;
	pthread_mutex_unlock(&(worker->mtx));
}

void worker_task_cancel(struct task_worker* worker, struct task_node* node){
	pthread_mutex_lock(&(worker->mtx));
	struct task_node* des = find_task_node_by_name(worker->worker_queue, node->name);
	des->cancel = 1;
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
