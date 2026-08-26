#include "scheduler_core.h"
#include <stdio.h>

int select_next_task_priority(SharedMemorySegment *shm) {
    int best_idx = -1;
    int highest_priority = -1;

    for (int i = 0; i < shm->task_count; i++) {
        Task *t = &shm->tasks[i];
        if (t->state == TASK_STATE_NEW || t->state == TASK_STATE_READY) {
            if (best_idx == -1 || t->priority > highest_priority) {
                highest_priority = t->priority;
                best_idx = i;
            }
        }
    }
    return best_idx;
}

int select_next_task_round_robin(SharedMemorySegment *shm, int *last_index) {
    if (shm->task_count <= 0) return -1;

    int start = (*last_index + 1) % shm->task_count;
    int idx = start;

    do {
        Task *t = &shm->tasks[idx];
        if (t->state == TASK_STATE_NEW || t->state == TASK_STATE_READY) {
            *last_index = idx;
            return idx;
        }
        idx = (idx + 1) % shm->task_count;
    } while (idx != start);

    return -1;
}
