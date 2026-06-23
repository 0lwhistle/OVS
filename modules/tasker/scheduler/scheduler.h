#ifndef SCHEDULER
#define SCHEDULER
#include "task_manager.h"
#include "task_worker.h"

struct scheduler_table{
	task_worker* sche_worker;
}

#endif // __SCHEDULER_H__