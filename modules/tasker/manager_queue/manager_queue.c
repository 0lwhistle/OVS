#include "manager_queue.h"

static int mq_init(struct mq_table* mq){
	if (!mq){
		ESP_LOGE(MQ_TAG, "mq is null, mq init fail!");
		return -1;
	}

	mq->table = task_manager_init();

	return 0;
}

static int s_mq_init(void){
	return mq_init(&s_mq_table);
}

static inline int mq_is_full(struct mq_table* mq) const {
	return mq->is_full;
}

static inline int mq_is_empty(struct mq_table* mq) const {
	return mq->is_empty;
}


static void mq_destory(struct mq_table* mq){

}

