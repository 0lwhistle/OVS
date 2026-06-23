#include "task_worker.h"

static int worker_init(void){

	struct task_worker little_worker;
	struct task_worker middle_worker;
	struct task_worker lots_worker;

	s_worker_queue.worker_queue = task_manager_init(32);
	s_worker_queue.src = NULL;
	s_worker_queue.cond = 

	if (!s_worker_queue){
		ESP_LOGE(TAG, "s_worker_queue init fail!");
		return TASK_MEM_ERR;
	}

	if(	worker_little_init(&little_worker) +
		worker_middle_init(&middle_worker) +
		worker_lots_init(&lots_worker) != 0){
			ESP_LOGE(TAG, "task_worker init fail!");
			return TASK_MEM_ERR;
		}

}



static int worker_little_init(struct task_worker* worker){
	printf("little pthread init");
	int ret = 0;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, LITTLE_TASK_STACK_SIZE);  // 设置栈大小
	ret = pthread_create(worker->pt, &attr, worker_little_handler, NULL);
	pthread_attr_destroy(&attr);

	worker->worker_queue = task_manager_init(4);
	worker->src = s_worker_queue;

	if (!worker->worker_queue){
		ret = TASK_MEM_ERR;
		return ret;
	}

	if (ret < 0){
		ESP_LOGE(TAG, "little pthread init fail!");
		return TASK_INNER_ERR
	}

	return TASK_OK;
}

static int worker_middle_init(struct task_worker* worker){
	printf("middle pthread init");
	int ret = 0;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, MIDDEL_TASK_STACK_SIZE);  // 设置栈大小
	ret = pthread_create(worker->pt, &attr, worker_middle_handler, NULL);
	pthread_attr_destroy(&attr);

	worker->worker_queue = task_manager_init(8);
	worker->src = s_worker_queue;

	if (!worker->worker_queue){
		ret = TASK_MEM_ERR;
		return ret;
	}

	if (ret < 0){
		ESP_LOGE(TAG, "little pthread init fail!");
		return TASK_INNER_ERR
	}

	return TASK_OK;

}

static int worker_lots_init(struct task_worker* worker){
	printf("lots pthread init");
	int ret = 0;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, LOTS_TASK_STACK_SIZE);  // 设置栈大小

	ret = pthread_create(worker->pt, &attr, worker_lots_handler, NULL);
	pthread_attr_destroy(&attr);

	worker->worker_queue = task_manager_init(16);
	worker->src = s_worker_queue;

	if (!worker->worker_queue){
		ret = TASK_MEM_ERR;
		return ret;
	}

	if (ret < 0){
		ESP_LOGE(TAG, "little pthread init fail!");
		return TASK_INNER_ERR
	}

	return TASK_OK;

}

static void worker_little_handler(struct task_worker* worker){	
	worker_handler(worker)
}

static void worker_middle_handler(struct task_worker* worker){
	worker_handler(worker)
}

static void worker_lots_handler(struct task_worker* worker){
	worker_handler(worker)
}

static void timer_callback(void* arg){
	int* timeout = (int*)arg;

	*timeout = 1;
	return;
}

static esp_timer_handle_t* timeout_timer_init(const int timeout, void* arg){

	if (!arg){
		ESP_LOGE(TASK_WORKER_TAG, "timer arg is null!");
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



static void worker_handler(struct task_worker* worker){
	int i = 0;
	int cnt = 0;
	int timeout_flag = 0;
	task_t status;
	while(!worker->stop){
		if(worker->worker_queue->is_empty)	{
			pthread_cond_wait(&worker->cond, &worker->mtx);
		}
		
		i = 0;
		cnt = worker->worker_queue->size;
		for (; i < cnt; ++i){
			if (!worker->worker_queue->queue[i].cancel && !worker->worker_queue->queue[i]->done){
				// start a oneshot timer, than run the task
				esp_timer_handle_t* timer = timeout_timer_init(worker->worker_queue->queue[i].timeout, &timeout_flag);
				status = worker->worker_queue->queue[i].fn(worker->worker_queue->queue[i].ctx);

				// delete the oneshot timer after taks
			
				ESP_ERROR_CHECK(esp_timer_delete(timer));
				if (status == TASK_OK){
					// set the done flag
					worker->worker_queue->queue[i].done = 1;

					// deal with the timeout case
					if (!timeout_flag){
						// if timeout and the task is not oneshot, 
						// enqueue the task to the long time cost queue.

					} else {

					}
				} else {
					ESP_LOGW(TASK_WORKER_TAG, "task node not done, cancel it.");
					task_cancel(worker->worker_queue->queue[i]);
				}


			}
		}
		wakeup_s_worker_queue(worker->src)
	}
}


static int worker_task_enqueue(struct task_worker* src, struct task_worker* des, struct task_node* node){
	while(des->worker_queue->is_full || src->worker_queue->is_empty){
		pthread_cond_wait(src->cond, src->mtx);
	}

	return 0;
}

static void worker_task_pop(struct task_worker* worker, struct task_node* node){


}

static inline void wakeup_s_worker_queue(struct task_worker* worker){
		pthread_mutex_lock(src->mtx);
		pthread_cond_broadcast(worker->src->cond);
		pthread_mutex_unlock(src->mtx); 
}