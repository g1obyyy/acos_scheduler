#include "scheduler_core.h"
#include <stdio.h>

void queue_init(TaskQueue *q) {
    q->size = 0;
    q->head = 0;
    q->tail = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        q->task_indices[i] = -1;
    }
}

int queue_is_empty(const TaskQueue *q) {
    return q->size == 0;
}

int queue_contains(const TaskQueue *q, int task_idx) {
    for (int i = 0; i < q->size; i++) {
        int idx = (q->head + i) % MAX_TASKS;
        if (q->task_indices[idx] == task_idx) {
            return 1;
        }
    }
    return 0;
}

int queue_push_tail(TaskQueue *q, int task_idx) {
    if (q->size >= MAX_TASKS) return -1;
    if (queue_contains(q, task_idx)) return 0; // prevent duplicates

    q->task_indices[q->tail] = task_idx;
    q->tail = (q->tail + 1) % MAX_TASKS;
    q->size++;
    return 0;
}

int queue_push_head(TaskQueue *q, int task_idx) {
    if (q->size >= MAX_TASKS) return -1;
    if (queue_contains(q, task_idx)) return 0;

    q->head = (q->head - 1 + MAX_TASKS) % MAX_TASKS;
    q->task_indices[q->head] = task_idx;
    q->size++;
    return 0;
}

int queue_pop_head(TaskQueue *q) {
    if (q->size == 0) return -1;

    int task_idx = q->task_indices[q->head];
    q->task_indices[q->head] = -1;
    q->head = (q->head + 1) % MAX_TASKS;
    q->size--;
    return task_idx;
}

int queue_remove(TaskQueue *q, int task_idx) {
    if (q->size == 0) return 0;

    int found = 0;
    int original_size = q->size;
    int temp[MAX_TASKS];
    int temp_size = 0;

    for (int i = 0; i < original_size; i++) {
        int idx = queue_pop_head(q);
        if (idx == task_idx) {
            found = 1;
        } else {
            temp[temp_size++] = idx;
        }
    }

    for (int i = 0; i < temp_size; i++) {
        queue_push_tail(q, temp[i]);
    }

    return found;
}

void queue_reorder_priority(SharedMemorySegment *shm, TaskQueue *q) {
    if (q->size <= 1) return;

    // Extract all elements into a temporary array
    int elems[MAX_TASKS];
    int count = q->size;
    for (int i = 0; i < count; i++) {
        elems[i] = queue_pop_head(q);
    }

    // Bubble sort or insertion sort stably by priority DESC, then original order (stable)
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            Task *t1 = &shm->tasks[elems[j]];
            Task *t2 = &shm->tasks[elems[j + 1]];

            // Higher priority first. If equal priority, preserve original relative order (stable sort)
            if (t1->priority < t2->priority) {
                int tmp = elems[j];
                elems[j] = elems[j + 1];
                elems[j + 1] = tmp;
            }
        }
    }

    // Push back into queue
    for (int i = 0; i < count; i++) {
        queue_push_tail(q, elems[i]);
    }
}

void apply_aging(SharedMemorySegment *shm) {
    if (shm->active_algorithm != SCHEDULER_ALGORITHM_PRIORITY) return;
    if (shm->ready_queue.size <= 1) return;

    int aged = 0;
        for (int i = 0; i < shm->ready_queue.size; i++) {
            int idx = shm->ready_queue.task_indices[(shm->ready_queue.head + i) % MAX_TASKS];
            Task *t = &shm->tasks[idx];

        // Increment wait ticks for tasks waiting in ready queue
        t->wait_ticks++;
        if (t->wait_ticks >= 5) { // Threshold for aging boost
            t->priority++;
            t->wait_ticks = 0;
            aged = 1;
            printf("[AGING] Task #%d waited too long, boosting priority to %d\n", t->id, t->priority);
        }
    }

    if (aged) {
        queue_reorder_priority(shm, &shm->ready_queue);
    }
}

void print_queues(SharedMemorySegment *shm) {
    printf("[READY QUEUE] ");
    if (shm->ready_queue.size == 0) {
        printf("(empty)");
    } else {
        for (int i = 0; i < shm->ready_queue.size; i++) {
            int idx = shm->ready_queue.task_indices[(shm->ready_queue.head + i) % MAX_TASKS];
            Task *t = &shm->tasks[idx];
            printf("#%d(p=%d)%s", t->id, t->priority, (i < shm->ready_queue.size - 1) ? " -> " : "");
        }
    }
    printf(" | [BLOCKED QUEUE] ");
    if (shm->blocked_queue.size == 0) {
        printf("(empty)\n");
    } else {
        for (int i = 0; i < shm->blocked_queue.size; i++) {
            int idx = shm->blocked_queue.task_indices[(shm->blocked_queue.head + i) % MAX_TASKS];
            Task *t = &shm->tasks[idx];
            printf("#%d(waiting=0x%X)%s", t->id, t->required_resources & ~t->held_resources, (i < shm->blocked_queue.size - 1) ? " -> " : "");
        }
        printf("\n");
    }
}

