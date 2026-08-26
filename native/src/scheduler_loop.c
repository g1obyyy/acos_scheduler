#include "scheduler_loop.h"
#include "scheduler_shm.h"
#include "scheduler_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>

static int safe_sem_wait(sem_t *sem) {
    int res;
    do {
        res = sem_wait(sem);
    } while (res == -1 && errno == EINTR);
    return res;
}

void run_scheduler_process_loop(const char *shm_name) {
    int fd = shm_open(shm_name, O_RDWR, 0666);
    if (fd == -1) return;
    SharedMemorySegment *shm = mmap(NULL, sizeof(SharedMemorySegment), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        close(fd);
        return;
    }

    while (!shm->shutdown_flag) {
        pthread_mutex_lock(&shm->mutex);
        
        int allocation_progress = 0;

        // 1. Compute globally held resources across active tasks
        unsigned int globally_held_resources = 0;
        for (int i = 0; i < shm->task_count; i++) {
            Task *t = &shm->tasks[i];
            if (t->state == TASK_STATE_DEADLOCK_ABORTED || t->state == TASK_STATE_FINISHED) {
                t->held_resources = 0;
                continue;
            }
            globally_held_resources |= t->held_resources;
        }

        // 2. Process Blocked Queue (Gradual Resource Allocation)
        int blocked_count = shm->blocked_queue.size;
        for (int i = 0; i < blocked_count; i++) {
            int task_idx = queue_pop_head(&shm->blocked_queue);
            if (task_idx == -1) continue;

            Task *t = &shm->tasks[task_idx];
            if (t->state == TASK_STATE_DEADLOCK_ABORTED || t->state == TASK_STATE_FINISHED) {
                t->held_resources = 0;
                continue;
            }

            unsigned int needed = t->required_resources & ~t->held_resources;
            if (needed == 0) {
                t->state = TASK_STATE_READY;
                queue_push_tail(&shm->ready_queue, task_idx);
                if (shm->active_algorithm == SCHEDULER_ALGORITHM_PRIORITY) {
                    queue_reorder_priority(shm, &shm->ready_queue);
                }
                allocation_progress = 1;
            } else {
                // Try to acquire one available resource at a time
                for (int bit = 0; bit < 32; bit++) {
                    unsigned int res = (1U << bit);
                    if ((needed & res) != 0) {
                        if ((res & globally_held_resources) == 0) {
                            t->held_resources |= res;
                            globally_held_resources |= res;
                            allocation_progress = 1;
                            break;
                        }
                    }
                }

                if (t->held_resources == t->required_resources) {
                    t->state = TASK_STATE_READY;
                    queue_push_tail(&shm->ready_queue, task_idx);
                    if (shm->active_algorithm == SCHEDULER_ALGORITHM_PRIORITY) {
                        queue_reorder_priority(shm, &shm->ready_queue);
                    }
                } else {
                    t->state = TASK_STATE_BLOCKED;
                    queue_push_tail(&shm->blocked_queue, task_idx);
                }
            }
        }

        // 3. Deadlock detection & resolution
        int resolved = detect_and_resolve_deadlocks(shm);
        if (resolved > 0) {
            allocation_progress = 1;
        }

        // Re-evaluate globally held resources after deadlock resolution
        globally_held_resources = 0;
        for (int i = 0; i < shm->task_count; i++) {
            Task *t = &shm->tasks[i];
            if (t->state == TASK_STATE_DEADLOCK_ABORTED || t->state == TASK_STATE_FINISHED) {
                t->held_resources = 0;
                continue;
            }
            globally_held_resources |= t->held_resources;
        }

        // Apply aging mechanism for priority scheduling to prevent starvation
        if (shm->active_algorithm == SCHEDULER_ALGORITHM_PRIORITY) {
            apply_aging(shm);
        }

        // 4. Task selection from Ready Queue
        int task_idx = -1;
        if (!queue_is_empty(&shm->ready_queue)) {
            task_idx = queue_pop_head(&shm->ready_queue);
        }

        if (task_idx != -1) {
            Task *t = &shm->tasks[task_idx];
            t->wait_ticks = 0; // Reset wait ticks upon selection
            shm->running_task_id = t->id;

            printf("[SCHEDULER][%s] Selected Task #%d (p=%d)\n", 
                   (shm->active_algorithm == SCHEDULER_ALGORITHM_PRIORITY) ? "PRIORITY" : "ROUND_ROBIN", 
                   t->id, t->priority);
            print_queues(shm);
            
            pthread_mutex_unlock(&shm->mutex);
            
            sem_post(&shm->worker_sem);
            safe_sem_wait(&shm->scheduler_sem);
            
            if (shm->shutdown_flag) break;
        } else {
            pthread_mutex_unlock(&shm->mutex);
            
            if (allocation_progress) {
                continue;
            }

            safe_sem_wait(&shm->scheduler_event_sem);
        }
    }

    sem_post(&shm->worker_sem);
    sem_post(&shm->scheduler_sem);
    sem_post(&shm->scheduler_event_sem);

    munmap(shm, sizeof(SharedMemorySegment));
    close(fd);
}

void run_worker_process_loop(const char *shm_name) {
    int fd = shm_open(shm_name, O_RDWR, 0666);
    if (fd == -1) return;
    SharedMemorySegment *shm = mmap(NULL, sizeof(SharedMemorySegment), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        close(fd);
        return;
    }

    while (!shm->shutdown_flag) {
        if (safe_sem_wait(&shm->worker_sem) == -1) {
            if (shm->shutdown_flag) break;
            continue;
        }

        if (shm->shutdown_flag) break;

        pthread_mutex_lock(&shm->mutex);
        int running_id = shm->running_task_id;
        Task *current_task = NULL;
        int current_task_idx = -1;
        for (int i = 0; i < shm->task_count; i++) {
            if (shm->tasks[i].id == running_id) {
                current_task = &shm->tasks[i];
                current_task_idx = i;
                break;
            }
        }

        if (current_task) {
            if (current_task->remaining_time_ms <= 0) {
                current_task->state = TASK_STATE_FINISHED;
                current_task->held_resources = 0;
                shm->running_task_id = -1;
                printf("[WORKER] Task #%d FINISHED\n", current_task->id);
                print_queues(shm);
                pthread_mutex_unlock(&shm->mutex);
                sem_post(&shm->scheduler_sem);
                sem_post(&shm->scheduler_event_sem);
                continue;
            }

            current_task->state = TASK_STATE_RUNNING;
            current_task->assigned_worker_pid = getpid();
            
            long quantum = shm->time_quantum_ms > 0 ? shm->time_quantum_ms : 50;
            long exec_time = current_task->remaining_time_ms < quantum ? current_task->remaining_time_ms : quantum;
            
            printf("[WORKER] Task #%d RUNNING quantum=%ldms (remaining=%ldms)\n", current_task->id, exec_time, current_task->remaining_time_ms);

            pthread_mutex_unlock(&shm->mutex);
            
            usleep(exec_time * 1000);
            
            pthread_mutex_lock(&shm->mutex);
            current_task->remaining_time_ms -= exec_time;
            
            if (current_task->remaining_time_ms <= 0) {
                current_task->state = TASK_STATE_FINISHED;
                current_task->held_resources = 0;
                printf("[WORKER] Task #%d FINISHED\n", current_task->id);
            } else {
                current_task->state = TASK_STATE_READY;
                // Re-queue task according to algorithm
                if (shm->active_algorithm == SCHEDULER_ALGORITHM_ROUND_ROBIN) {
                    queue_push_tail(&shm->ready_queue, current_task_idx);
                    printf("[WORKER] Task #%d PREEMPTED (Round Robin). Re-queued at tail.\n", current_task->id);
                } else {
                    queue_push_tail(&shm->ready_queue, current_task_idx);
                    queue_reorder_priority(shm, &shm->ready_queue);
                    printf("[WORKER] Task #%d PREEMPTED (Priority). Re-ordered.\n", current_task->id);
                }
            }

            shm->running_task_id = -1;
            current_task->assigned_worker_pid = 0;
            print_queues(shm);
        } else {
            fprintf(stderr, "[WORKER ERROR] running_task_id %d does not match any existing task\n", running_id);
            shm->running_task_id = -1;
        }
        pthread_mutex_unlock(&shm->mutex);

        sem_post(&shm->scheduler_sem);
        sem_post(&shm->scheduler_event_sem);
    }

    munmap(shm, sizeof(SharedMemorySegment));
    close(fd);
}

