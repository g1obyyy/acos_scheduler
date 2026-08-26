#include <jni.h>
#include "scheduler_shm.h"
#include "ipc_sync.h"
#include "process_mgmt.h"
#include "scheduler_core.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>

static SharedMemorySegment *current_shm = NULL;
static char current_shm_name[256] = "/task_scheduler_shm";
static pid_t scheduler_pid = -1;
static pid_t worker_pid = -1;

JNIEXPORT void JNICALL Java_com_taskscheduler_nativebridge_NativeScheduler_initialize
  (JNIEnv *env, jobject thiz, jstring shmName) {
    const char *name = (*env)->GetStringUTFChars(env, shmName, NULL);
    if (name == NULL) return;

    strncpy(current_shm_name, name, sizeof(current_shm_name) - 1);
    current_shm_name[sizeof(current_shm_name) - 1] = '\0';

    logger_init(current_shm_name, "JNI");
    logger_log(LOG_LEVEL_INFO, "Initializing scheduler with SHM: %s", current_shm_name);

    current_shm = create_or_open_shm(current_shm_name, 1);
    if (current_shm != NULL) {
        init_shm_content(current_shm);
        queue_init(&current_shm->ready_queue);
        queue_init(&current_shm->blocked_queue);
        logger_log(LOG_LEVEL_INFO, "Shared memory initialized successfully: %s", current_shm_name);
    } else {
        logger_log(LOG_LEVEL_ERROR, "Failed to create/open shared memory: %s", current_shm_name);
    }

    (*env)->ReleaseStringUTFChars(env, shmName, name);
}

JNIEXPORT void JNICALL Java_com_taskscheduler_nativebridge_NativeScheduler_start
  (JNIEnv *env, jobject thiz) {
    if (current_shm == NULL) return;
    
    logger_log(LOG_LEVEL_INFO, "Starting scheduler and worker processes for SHM: %s", current_shm_name);
    current_shm->shutdown_flag = 0;
    scheduler_pid = spawn_scheduler_process(current_shm_name);
    worker_pid = spawn_worker_process(current_shm_name);
    logger_log(LOG_LEVEL_INFO, "Processes spawned: scheduler_pid=%d, worker_pid=%d", (int)scheduler_pid, (int)worker_pid);
}

JNIEXPORT void JNICALL Java_com_taskscheduler_nativebridge_NativeScheduler_stop
  (JNIEnv *env, jobject thiz) {
    logger_log(LOG_LEVEL_INFO, "Stopping scheduler and worker processes");
    if (current_shm != NULL) {
        current_shm->shutdown_flag = 1;
        sem_post(&current_shm->worker_sem);
        sem_post(&current_shm->scheduler_sem);
        sem_post(&current_shm->scheduler_event_sem);
    }

    if (scheduler_pid > 0) {
        wait_for_process(scheduler_pid);
        scheduler_pid = -1;
    }

    if (worker_pid > 0) {
        wait_for_process(worker_pid);
        worker_pid = -1;
    }

    if (current_shm != NULL) {
        close_and_unlink_shm(current_shm_name, current_shm, 1);
        current_shm = NULL;
    }
    logger_log(LOG_LEVEL_INFO, "Scheduler stopped and cleaned up");
    logger_close();
}

JNIEXPORT jint JNICALL Java_com_taskscheduler_nativebridge_NativeScheduler_submitTask
  (JNIEnv *env, jobject thiz, jint id, jint priority, jlong totalTimeMs, jint requiredResources) {
    if (current_shm == NULL) return -1;

    pthread_mutex_lock(&current_shm->mutex);
    if (current_shm->task_count >= MAX_TASKS) {
        logger_log(LOG_LEVEL_ERROR, "Task submission failed: task count reached MAX_TASKS (%d)", MAX_TASKS);
        pthread_mutex_unlock(&current_shm->mutex);
        return -1;
    }

    int idx = current_shm->task_count;
    current_shm->tasks[idx].id = id;
    current_shm->tasks[idx].base_priority = priority;
    current_shm->tasks[idx].effective_priority = priority;
    current_shm->tasks[idx].state = (requiredResources == 0) ? TASK_STATE_READY : TASK_STATE_NEW;
    current_shm->tasks[idx].total_time_ms = totalTimeMs;
    current_shm->tasks[idx].remaining_time_ms = totalTimeMs;
    current_shm->tasks[idx].required_resources = requiredResources;
    current_shm->tasks[idx].held_resources = 0;
    current_shm->tasks[idx].assigned_worker_pid = -1;
    current_shm->tasks[idx].wait_ticks = 0;

    current_shm->task_count++;

    if (requiredResources == 0) {
        queue_push_tail(&current_shm->ready_queue, idx);
            queue_reorder_priority(current_shm, &current_shm->ready_queue);
    } else {
        queue_push_tail(&current_shm->blocked_queue, idx);
    }

    logger_log(LOG_LEVEL_INFO, "Submitted Task #%d base_priority=%d required_resources=0x%X", id, priority, requiredResources);
    log_scheduler_snapshot(current_shm, "TASK SUBMITTED");
    pthread_mutex_unlock(&current_shm->mutex);

    sem_post(&current_shm->scheduler_event_sem);
    return id;
}

JNIEXPORT void JNICALL Java_com_taskscheduler_nativebridge_NativeScheduler_changePriority
  (JNIEnv *env, jobject thiz, jint taskId, jint priority) {
    if (current_shm == NULL) return;

    pthread_mutex_lock(&current_shm->mutex);
    for (int i = 0; i < current_shm->task_count; i++) {
        if (current_shm->tasks[i].id == taskId) {
            int old_base = current_shm->tasks[i].base_priority;
            int old_eff = current_shm->tasks[i].effective_priority;

            current_shm->tasks[i].base_priority = priority;
            current_shm->tasks[i].effective_priority = priority;
            current_shm->tasks[i].wait_ticks = 0;

            if (queue_contains(&current_shm->ready_queue, i)) {
                queue_reorder_priority(current_shm, &current_shm->ready_queue);
            }
            logger_log(LOG_LEVEL_INFO, "[PRIORITY CHANGE] Task #%d: base %d -> %d, effective %d -> %d",
                       taskId, old_base, priority, old_eff, priority);
            log_scheduler_snapshot(current_shm, "PRIORITY CHANGED");
            break;
        }
    }
    pthread_mutex_unlock(&current_shm->mutex);
    sem_post(&current_shm->scheduler_event_sem);
}

JNIEXPORT jint JNICALL Java_com_taskscheduler_nativebridge_NativeScheduler_getTaskCount
  (JNIEnv *env, jobject thiz) {
    if (current_shm == NULL) return 0;
    pthread_mutex_lock(&current_shm->mutex);
    int count = current_shm->task_count;
    pthread_mutex_unlock(&current_shm->mutex);
    return count;
}

