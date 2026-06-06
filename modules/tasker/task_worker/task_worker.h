#ifndef TASK_WORKER
#define TASK_WORKER

#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

struct task_worker{
	pthread_t little;
	pthread_t middle;
	pthread_t lots;



};



static int worker_init(struct task_worker* worker);

static void worker_handle(struct task_node* node){
	

}



#endif // __TASK_WORKER_H__