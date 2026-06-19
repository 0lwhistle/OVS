#include "task_worker.h"

static int worker_init(void){

	struct task_worker little_worker;
	struct task_worker middle_worker;
	struct task_worker lots_worker;

	s_worker_queue = task_manager_init(32);

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
	pthread_create(worker->pt, &attr, worker_little_handler, NULL);
	pthread_attr_destroy(&attr);

	worker->worker_queue = task_manager_init(4);

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
				esp_timer_handle_t* timer = timeout_timer_init(worker->worker_queue->queue[i].timeout, &timeout_flag);
				status = worker->worker_queue->queue[i].fn(worker->worker_queue->queue[i].ctx);
				ESP_ERROR_CHECK(esp_timer_delete(timer));
				if (status == TASK_OK){
					worker->worker_queue->queue[i].done = 1;
					if (!timeout_flag){

					} else {

					}
				} else {
					ESP_LOGW(TASK_WORKER_TAG, "task node not done, cancel it.");
					task_cancel(worker->worker_queue->queue[i]);
				}


			}
		}
		

	}
}


static int worker_task_enqueue(struct task_worker* src, struct task_worker* des, struct task_node* node){
	if (des->worker_queue->is_full){
		pthread_cond_wait(src->cond, src->mtx);
	}

	return 0;
}

static void worker_task_pop(struct task_worker* worker, struct task_node* node){


}