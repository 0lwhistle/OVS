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

static void worker_handler(struct task_worker* worker){
	int i = 0;
	int cnt = 0;
	while(!worker->stop){
		if(worker->worker_queue->is_empty)	{
			pthread_cond_wait(&worker->cond, &worker->mtx);
		}
		
		i = 0;
		cnt = worker->worker_queue->size;
		for (; i < cnt; ++i){
			if (!worker->worker_queue->queue[i]->done){
				// 创建定时器时传入参数
				const esp_timer_create_args_t oneshot_args = {
					.callback = &timer_callback,
					.arg = params,  // 关键：传递参数指针
					.name = "worker_handler_timer"
				};
				
				esp_timer_handle_t oneshot_timer;
				ESP_ERROR_CHECK(esp_timer_create(&oneshot_args, &oneshot_timer));
				ESP_ERROR_CHECK(esp_timer_start_once(oneshot_timer, worker->worker_queue->queue[i].timeout * 1000));
				worker->worker_queue->queue[i].fn(worker->worker_queue->queue[i].ctx);
				
			}
		}
		

	}
}