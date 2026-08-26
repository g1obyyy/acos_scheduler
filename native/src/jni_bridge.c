#include <jni.h>
#include "scheduler_shm.h"
#include "ipc_sync.h"
#include "process_mgmt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>

static SharedMemorySegment *current_shm = NULL;
static pid_t scheduler_pid = -1;
static pid_t worker_pid = -1;

JNIEXPORT void JNICALL Java_com_taskscheduler_nativebridge_NativeScheduler_initialize
  (JNIEnv *env, jobject thiz, jstring shmName) {
    const char *name = (*env)->GetStringUTFChars(env, shmName, NULL);
    if (name == NULL) return;

    current_shm = create_or_open_shm(name, 1);
    if (current_shm != NULL) {
        init_shm_content(current_shm);
    }

    (*env)->ReleaseStringUTFChars(env, shmName, name);
}

JNIEXPORT void JNICALL Java_com_taskscheduler_nativebridge_NativeScheduler_start
  (JNIEnv *env, jobject thiz) {
    if (current_shm == NULL) return;
    
    current_shm->shutdown_flag = 0;
    scheduler_pid = spawn_scheduler_process(SHM_NAME);
    worker_pid = spawn_worker_process(SHM_NAME);
}

JNIEXPORT void JNICALL Java_com_taskscheduler_nativebridge_NativeScheduler_stop
  (JNIEnv *env, jobject thiz) {
    if (current_shm != NULL) {
        current_shm->shutdown_flag = 1;
        // Wake up sleeping processes on all semaphores
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
        close_and_unlink_shm(SHM_NAME, current_shm, 1);
        current_shm = NULL;
    }
}

JNIEXPORT jint JNICALL Java_com_taskscheduler_nativebridge_NativeScheduler_submitTask
  (JNIEnv *env, jobject thiz, jint id, jint priority, jlong totalTimeMs, jint requiredResources) {
    if (current_shm == NULL) return -1;

    pthread_mutex_lock(&current_shm->mutex);
    if (current_shm->task_count >= MAX_TASKS) {
        pthread_mutex_unlock(&current_shm->mutex);
        return -1;
    }

    int idx = current_shm->task_count;
    current_shm->tasks[idx].id = id;
    current_shm->tasks[idx].priority = priority;
    current_shm->tasks[idx].state = TASK_STATE_NEW;
    current_shm->tasks[idx].total_time_ms = totalTimeMs;
    current_shm->tasks[idx].remaining_time_ms = totalTimeMs;
    current_shm->tasks[idx].required_resources = requiredResources;
    current_shm->tasks[idx].held_resources = 0;
    current_shm->tasks[idx].assigned_worker_pid = -1;

    current_shm->task_count++;
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
            current_shm->tasks[i].priority = priority;
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
