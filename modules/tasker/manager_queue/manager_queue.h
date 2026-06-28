#ifndef MENAGER_QUEUE
#define MENAGER_QUEUE

#include "task_manager.h"
#include "task_worker.h"

static const char* MQ_TAG = "[MANAGER_QUEUE]";

struct mq_table{
	int is_full;
	int is_empty;
	task_manager* table;
}

static struct mq_table s_mq_table;

static int mq_init(struct mq_table* mq);
static int s_mq_init(void);

static inline int mq_is_empty(struct mq_table* mq);
static inline int mq_is_full(struct mq_table* mq);


static void mq_destory(struct mq_table* mq);

#endif __MENAGER_QUEUE__