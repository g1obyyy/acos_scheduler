#ifndef SCHEDULER_SHM_H
#define SCHEDULER_SHM_H

#include <pthread.h>
#include <semaphore.h>

#define MAX_TASKS 256
#define SHM_NAME "/task_scheduler_shm"

typedef enum {
    SCHEDULER_ALGORITHM_PRIORITY = 0,
    SCHEDULER_ALGORITHM_ROUND_ROBIN = 1
} SchedulerAlgorithm;

typedef enum {
    TASK_STATE_NEW = 0,
    TASK_STATE_READY = 1,
    TASK_STATE_RUNNING = 2,
    TASK_STATE_BLOCKED = 3,
    TASK_STATE_FINISHED = 4,
    TASK_STATE_DEADLOCK_ABORTED = 5
} TaskStateEnum;

typedef struct {
    int id;
    int priority;
    int state;
    long total_time_ms;
    long remaining_time_ms;
    unsigned int required_resources;
    unsigned int held_resources;
    int assigned_worker_pid;
} Task;

typedef struct {
    Task tasks[MAX_TASKS];
    int task_count;
    int running_task_id;
    
    pthread_mutex_t mutex;
    sem_t worker_sem;
    sem_t scheduler_sem;
    sem_t scheduler_event_sem;
    int shutdown_flag;
    int active_algorithm;
    long time_quantum_ms;
} SharedMemorySegment;

SharedMemorySegment* create_or_open_shm(const char *name, int is_creator);
void init_shm_content(SharedMemorySegment *shm);
void close_and_unlink_shm(const char *name, SharedMemorySegment *shm, int is_creator);

int detect_and_resolve_deadlocks(SharedMemorySegment *shm);

#endif