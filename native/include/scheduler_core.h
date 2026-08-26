#ifndef SCHEDULER_CORE_H
#define SCHEDULER_CORE_H

#include "scheduler_shm.h"

void queue_init(TaskQueue *q);
int queue_is_empty(const TaskQueue *q);
int queue_push_tail(TaskQueue *q, int task_idx);
int queue_push_head(TaskQueue *q, int task_idx);
int queue_pop_head(TaskQueue *q);
int queue_remove(TaskQueue *q, int task_idx);
int queue_contains(const TaskQueue *q, int task_idx);
void queue_reorder_priority(SharedMemorySegment *shm, TaskQueue *q);
void apply_aging(SharedMemorySegment *shm);

void print_queues(SharedMemorySegment *shm);

#endif
