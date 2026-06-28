#include "task_worker.h"



static int worker_init(void){

	int ret = 0;

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


static int worker_little_init(void){
	struct task_worker* worker = s_task_worker_ctx.little_worker;
	printf("little pthread init");
	int ret = TASK_OK;
	
	worker->worker_queue = task_manager_init(LITTLE_TASK_QUEUE_SIZE);
	if (!worker->worker_queue){
		printf("little woker task manager init fail.");
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
	ret = pthread_create(worker->pt, &attr, worker_little_handler, worker);
	pthread_attr_destroy(&attr);

	return ret;
}

static int worker_middle_init(void){
	printf("middle pthread init");
	struct task_worker* worker = s_task_worker_ctx.middle_worker;

	int ret = TASK_OK;
	
	worker->worker_queue = task_manager_init(MIDDEL_TASK_QUEUE_SIZE);

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
	pthread_attr_setstacksize(&attr, MIDDEL_TASK_STACK_SIZE);  // 设置栈大小
	ret = pthread_create(worker->pt, &attr, worker_middle_handler, worker);
	pthread_attr_destroy(&attr);

	return ret;

}

static int worker_lots_init(void){
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
	ret = pthread_create(worker->pt, &attr, worker_lots_handler, worker);
	pthread_attr_destroy(&attr);

	
	return ret;

}

static int worker_dispatcher_init(void){

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
	ret = pthread_create(worker->pt, &attr, worker_dispatcher_handler, worker);
	pthread_attr_destroy(&attr);

	return ret;
}


static int worker_sched_init(void){

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
	ret = pthread_create(worker->pt, &attr, worker_sched_handler, worker);
	pthread_attr_destroy(&attr);

	return ret;
}


static void worker_little_handler(struct task_worker* worker){	
	worker_do_handler(worker);
}

static void worker_middle_handler(struct task_worker* worker){
	worker_do_handler(worker);
}

static void worker_lots_handler(struct task_worker* worker){
	worker_do_handler(worker);
}

static void worker_dispatcher_handler(struct task_worker* worker){
	int i = 0;
	int size = 0;
	int timeout_flag = 0;
	enum task_t status;
	worker->is_working = 1;
	worker->stop = 0;
	while(!worker->stop){
		pthread_mutex_lock(&(worker->mtx));
		while(worker->worker_queue->is_empty)	{
			pthread_cond_wait(&worker->cond, &worker->mtx);
		}
		
		i = 0;
		size = worker->worker_queue->size;
		struct task_manager* worker_queue = worker->worker_queue;
		for (; i < size; ++i){
			if (!worker_queue->queue[i].cancel && !worker_queue->queue[i].done){
				// Have to use deep copy to dispatch the task, avoiding the memery error
				struct task_node* node = task_init(worker_queue->queue[i].timeout,
					worker_queue->queue[i].pri,
					worker_queue->queue[i].level,
					worker_queue->queue[i].name,
					worker_queue->queue[i].fn,
					worker_queue->queue[i].ctx
				);
					
				enqueue_switcher(node);
				task_cancel(&(worker_queue->queue[i]));
			}
			
		}
		task_manager_pri_sort(worker->worker_queue->queue);
		pthread_mutex_unlock(&(worker->mtx));
	}
}

static void worker_sched_handler(struct task_worker* worker){

}

static void timer_callback(void* arg){
	int* timeout = (int*)arg;

	*timeout = 1;
	return;
}

static esp_timer_handle_t* timeout_timer_init(const int timeout, void* arg){

	if (!arg){
		ESP_LOGE(TASK_WORKER_TASK_WORKER_TAG, "timer arg is null!");
		return NULL;
	}

	// 创建定时器时传入参数
	const esp_timer_create_args_t oneshot_args = {
		.callback = &timer_callback,
		.arg = arg,  // 关键：传递参数指针
		.name = "worker_handler_timer"
	};
	
	esp_timer_handle_t oneshot_timer;
	ESP_ERROR_CHECK(esp_timer_create(&oneshot_args, &oneshot_timer));
	ESP_ERROR_CHECK(esp_timer_start_once(oneshot_timer, timeout * 1000));

	return &oneshot_timer;
}



static void worker_do_handler(struct task_worker* worker){

	int i = 0;
	int size = 0;
	int timeout_flag = 0;
	enum task_t status;
	worker->is_working = 1;
	worker->stop = 0;
	while(!worker->stop){
		pthread_mutex_lock(&(worker->mtx));
		while(worker->worker_queue->is_empty)	{
			pthread_cond_wait(&worker->cond, &worker->mtx);
		}
		
		i = 0;
		size = worker->worker_queue->size;
		struct task_manager* worker_queue = worker->worker_queue;
		for (; i < size; ++i){
			if (!worker_queue->queue[i].cancel && !worker_queue->queue[i].done){
				// start a oneshot timer, than run the task
				esp_timer_handle_t* timer = timeout_timer_init(worker_queue->queue[i].timeout, &timeout_flag);
				status = worker_queue->queue[i].fn(worker_queue->queue[i].ctx);

				// delete the oneshot timer after taks
				ESP_ERROR_CHECK(esp_timer_delete(timer));
				if (status == TASK_OK){
					// set the done flag
					task_done(&(worker_queue->queue[i]));

					// log the timeout case
					worker_queue->queue[i].is_timeout = timeout_flag;

				} else {
					ESP_LOGW(TASK_WORKER_TASK_WORKER_TAG, "task node not done, cancel it.");
					task_cancel(&(worker_queue->queue[i]));
				}
			}
		}
		pthread_mutex_unlock(&(worker->mtx));
	}
}


static int worker_task_enqueue(struct task_worker* des, struct task_node* node){
	if (des->stop){
		ESP_LOGW(TASK_WORKER_TAG, "worker is already stop.");
		return -1;
	}
	if(des->worker_queue->is_full){
		ESP_LOGW(TASK_WORKER_TAG, "worker is full now.");
		task_node_pri_up(node);
		return -1;
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
			ESP_LOGI(TASK_WORKER_TAG, "task: %s enqueue successfully.", worker_queue->queue[i].name);

			worker_queue->is_empty = 0;
			pthread_cond_signal(&(des->cond));

			pthread_mutex_unlock(&(des->mtx));

			return 0;
		}
	}

	worker_queue->is_empty = 0;
	worker_queue->is_full = 1;
	pthread_mutex_unlock(&(des->mtx));

	return -1;
}

static inline int enqueue_switcher(struct task_node* node){
	switch (node->level){
		case little:
			worker_task_enqueue(s_task_worker_ctx.little_worker, node);
			return TASK_OK;
		
		case middle:
			worker_task_enqueue(s_task_worker_ctx.middle_worker, node);
			return TASK_OK;

		case lots:
			worker_task_enqueue(s_task_worker_ctx.lots_worker, node);
			return TASK_OK;

		default:
			printf("unknow level");
			return TASK_INNER_ERR;
	}
}


static void worker_task_done(struct task_worker* worker, struct task_node* node){
	pthread_mutex_lock(&(worker->mtx));
	struct task_node* des = find_task_node_by_name(worker->worker_queue, node->name);
	des->done = 1;
	pthread_mutex_unlock(&(worker->mtx));
}

static void worker_task_cancel(struct task_worker* worker, struct task_node* node){
	pthread_mutex_lock(&(worker->mtx));
	struct task_node* des = find_task_node_by_name(worker->worker_queue, node->name);
	des->cancel = 1;
	pthread_mutex_unlock(&(worker->mtx));
}



static void worker_delete(struct task_worker* worker){
	


}
