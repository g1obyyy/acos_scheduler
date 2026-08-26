#ifndef SCHEDULER_CORE_H
#define SCHEDULER_CORE_H

#include "scheduler_shm.h"

int select_next_task_priority(SharedMemorySegment *shm);
int select_next_task_round_robin(SharedMemorySegment *shm, int *last_index);

#endif
