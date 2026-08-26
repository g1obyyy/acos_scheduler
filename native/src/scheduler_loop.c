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

    int rr_last_index = -1;

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

        // 2. Gradual resource allocation (one resource at a time per pass)
        for (int i = 0; i < shm->task_count; i++) {
            Task *t = &shm->tasks[i];
            if (t->state == TASK_STATE_DEADLOCK_ABORTED || t->state == TASK_STATE_FINISHED) {
                continue;
            }
            if (t->state == TASK_STATE_NEW || t->state == TASK_STATE_BLOCKED) {
                unsigned int needed = t->required_resources & ~t->held_resources;
                if (needed == 0) {
                    if (t->state != TASK_STATE_READY) {
                        t->state = TASK_STATE_READY;
                        allocation_progress = 1;
                    }
                } else {
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
                    } else {
                        t->state = TASK_STATE_BLOCKED;
                    }
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

        // 4. Task selection
        int task_idx = -1;
        if (shm->active_algorithm == SCHEDULER_ALGORITHM_ROUND_ROBIN) {
            task_idx = select_next_task_round_robin(shm, &rr_last_index);
        } else {
            task_idx = select_next_task_priority(shm);
        }

        if (task_idx != -1) {
            Task *t = &shm->tasks[task_idx];
            shm->running_task_id = t->id;
            
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
        for (int i = 0; i < shm->task_count; i++) {
            if (shm->tasks[i].id == running_id) {
                current_task = &shm->tasks[i];
                break;
            }
        }

        if (current_task) {
            if (current_task->state != TASK_STATE_READY) {
                fprintf(stderr, "[WORKER ERROR] Invalid task state %d for assigned task ID %d\n", current_task->state, current_task->id);
                shm->running_task_id = -1;
                current_task->assigned_worker_pid = 0;
                pthread_mutex_unlock(&shm->mutex);
                sem_post(&shm->scheduler_sem);
                sem_post(&shm->scheduler_event_sem);
                continue;
            }

            if (current_task->remaining_time_ms <= 0) {
                current_task->state = TASK_STATE_FINISHED;
                current_task->held_resources = 0;
                shm->running_task_id = -1;
                pthread_mutex_unlock(&shm->mutex);
                sem_post(&shm->scheduler_sem);
                sem_post(&shm->scheduler_event_sem);
                continue;
            }

            current_task->state = TASK_STATE_RUNNING;
            current_task->assigned_worker_pid = getpid();
            
            long quantum = shm->time_quantum_ms > 0 ? shm->time_quantum_ms : 50;
            long exec_time = current_task->remaining_time_ms < quantum ? current_task->remaining_time_ms : quantum;
            
            pthread_mutex_unlock(&shm->mutex);
            
            usleep(exec_time * 1000);
            
            pthread_mutex_lock(&shm->mutex);
            current_task->remaining_time_ms -= exec_time;
            
            if (current_task->remaining_time_ms <= 0) {
                current_task->state = TASK_STATE_FINISHED;
                current_task->held_resources = 0;
            } else {
                current_task->state = TASK_STATE_READY;
            }

            shm->running_task_id = -1;
            current_task->assigned_worker_pid = 0;
        } else {
            fprintf(stderr, "[WORKER ERROR] running_task_id %d does not match any existing task\n", running_id);
            shm->running_task_id = -1;
        }
        pthread_mutex_unlock(&shm->mutex);

        // Handshake completion only: worker_sem completed, notify scheduler via scheduler_sem
        sem_post(&shm->scheduler_sem);
    }

    munmap(shm, sizeof(SharedMemorySegment));
    close(fd);
}

