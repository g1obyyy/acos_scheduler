/*
 * JNI-мост между Java-классом NativeScheduler и native C-реализацией планировщика.
 */

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


/*
 * Указатель на SharedMemorySegment, к которому подключен JNI-процесс.
 *
 */
static SharedMemorySegment *current_shm = NULL;


/*
 * Имя текущего POSIX Shared Memory object.
 */
static char current_shm_name[256] = "/task_scheduler_shm";


/*
 * PID запущенных дочерних процессов.
 */
static pid_t scheduler_pid = -1;
static pid_t worker_pid = -1;


/*
 * Java:
 *
 * NativeScheduler.initialize(String shmName)
 *
 * Инициализирует native-часть планировщика:
 * - получает имя shared memory из Java;
 * - создает POSIX Shared Memory;
 * - инициализирует общие структуры;
 * - создает ready_queue и blocked_queue;
 * - запускает logger для JNI-компонента.
 */
JNIEXPORT void JNICALL
Java_com_taskscheduler_nativebridge_NativeScheduler_initialize(JNIEnv *env, jobject thiz, jstring shmName) {
    const char *name = (*env)->GetStringUTFChars(env, shmName, NULL);
    if (name == NULL) {
        return;
    }

    strncpy(current_shm_name, name, sizeof(current_shm_name) - 1);
    current_shm_name[sizeof(current_shm_name) - 1] = '\0';


    /*
     * Инициализируем logger для JNI-процесса.
     */
    logger_init(current_shm_name, "JNI");
    logger_log(LOG_LEVEL_INFO, "Initializing scheduler with SHM: %s", current_shm_name);


    /*
     * JNI является creator shared memory.
     */
    current_shm = create_or_open_shm(current_shm_name, 1);
    if (current_shm != NULL) {
        /*
         * Первоначально обнуляем SharedMemorySegment и создаем process-shared mutex/semaphore.
         */
        init_shm_content(current_shm);

        /*
         * Инициализируем две реальные очереди
         */
        queue_init(&current_shm->ready_queue);
        queue_init(&current_shm->blocked_queue);
        logger_log(LOG_LEVEL_INFO, "Shared memory initialized successfully: %s", current_shm_name);
    } else {
        logger_log(LOG_LEVEL_ERROR, "Failed to create/open shared memory: %s", current_shm_name);
    }

    (*env)->ReleaseStringUTFChars(env, shmName, name);
}


/*
 * Java:
 *
 * NativeScheduler.start()
 *
 * Запускает отдельные Scheduler и Worker процессы.
 */
JNIEXPORT void JNICALL
Java_com_taskscheduler_nativebridge_NativeScheduler_start(JNIEnv *env, jobject thiz) {
    if (current_shm == NULL) {
        return;
    }

    logger_log(LOG_LEVEL_INFO, "Starting scheduler and worker processes for SHM: %s", current_shm_name);


    /*
     * Сбрасываем shutdown flag перед запуском.
     */
    current_shm->shutdown_flag = 0;


    /*
     * Создаем два отдельных дочерних процесса:
     * Оба подключаются к одной shared memory по current_shm_name.
     */
    scheduler_pid = spawn_scheduler_process(current_shm_name);
    worker_pid = spawn_worker_process(current_shm_name);
    logger_log(LOG_LEVEL_INFO, "Processes spawned: scheduler_pid=%d, worker_pid=%d", (int)scheduler_pid, (int)worker_pid);
}


/*
 * Java:
 *
 * NativeScheduler.stop()
 *
 * Корректно завершает Scheduler и Worker, после чего освобождает shared memory.
 */
JNIEXPORT void JNICALL
Java_com_taskscheduler_nativebridge_NativeScheduler_stop(JNIEnv *env, jobject thiz) {
    logger_log(LOG_LEVEL_INFO, "Stopping scheduler and worker processes");
    if (current_shm != NULL) {

        /*
         * Сообщаем Scheduler и Worker, что приложение должно завершиться.
         */
        current_shm->shutdown_flag = 1;

        /*
         * процесс может в этот момент спать внутри sem_wait().
         *
         * Поэтому выполняем sem_post() для всех семафоров,
         * на которых потенциально могут ожидать Scheduler/Worker.
         */
        sem_post(&current_shm->worker_sem);
        sem_post(&current_shm->scheduler_sem);
        sem_post(&current_shm->scheduler_event_sem);
    }


    /*
     * Дожидаемся фактического завершения Scheduler.
     */
    if (scheduler_pid > 0) {
        wait_for_process(scheduler_pid);
        scheduler_pid = -1;
    }

    /*
     * Аналогично дожидаемся завершения Worker.
     */
    if (worker_pid > 0) {
        wait_for_process(worker_pid);
        worker_pid = -1;
    }


    /*
     * Shared memory уничтожаем только после того, как оба дочерних процесса завершились.
     */
    if (current_shm != NULL) {
        close_and_unlink_shm(current_shm_name, current_shm, 1);
        current_shm = NULL;
    }
    logger_log(LOG_LEVEL_INFO, "Scheduler stopped and cleaned up");

    /*
     * Закрываем log-файл JNI-процесса.
     */
    logger_close();
}


/*
 * Java:
 *
 * NativeScheduler.submitTask(
 *     int id,
 *     int priority,
 *     long totalTimeMs,
 *     int requiredResources
 * )
 *
 * Создает новую Task в shared memory и помещает ее либо в ready_queue, либо в blocked_queue.
 *
 * Возвращает ID созданной задачи либо -1 при ошибке.
 */
JNIEXPORT jint JNICALL
Java_com_taskscheduler_nativebridge_NativeScheduler_submitTask(JNIEnv *env, jobject thiz, jint id, jint priority, jlong totalTimeMs, jint requiredResources) {
    if (current_shm == NULL) {
        return -1;
    }

    pthread_mutex_lock(&current_shm->mutex);

    // Валидация входных данных (Negative Test support)
    if (id < 0 || priority < 0 || totalTimeMs <= 0) {
        logger_log(LOG_LEVEL_ERROR, "Validation failed: invalid task parameters (id=%d, priority=%d, time=%ld)", (int)id, (int)priority, (long)totalTimeMs);
        pthread_mutex_unlock(&current_shm->mutex);
        return -1;
    }

    // Проверка уникальности ID задачи
    for (int i = 0; i < current_shm->task_count; i++) {
        if (current_shm->tasks[i].id == id) {
            logger_log(LOG_LEVEL_ERROR, "Validation failed: task ID %d already exists", (int)id);
    pthread_mutex_unlock(&current_shm->mutex);
            return -1;
        }
    }

    /*
     * Проверяем, осталось ли место для новой задачи.
     */
    if (current_shm->task_count >= MAX_TASKS) {
        logger_log(LOG_LEVEL_ERROR, "Task submission failed: task count reached MAX_TASKS (%d)", MAX_TASKS);
        pthread_mutex_unlock(&current_shm->mutex);
        return -1;
    }

    int idx = current_shm->task_count;
    current_shm->tasks[idx].id = id;

    /*
     * base_priority:
     * исходный приоритет от пользователя.
     *
     * effective_priority:
     * текущий приоритет, по которому реально работает Scheduler.
     */
    current_shm->tasks[idx].base_priority = priority;
    current_shm->tasks[idx].effective_priority = priority;


    /*
     * Если ресурсы не нужны — задача сразу READY.
     *
     * Если ресурсы нужны — первоначально NEW,
     * после чего Scheduler начнет постепенно выдавать ей ресурсы.
     */
    current_shm->tasks[idx].state = (requiredResources == 0) ? TASK_STATE_READY : TASK_STATE_NEW;
    current_shm->tasks[idx].total_time_ms = totalTimeMs;
    current_shm->tasks[idx].remaining_time_ms = totalTimeMs;
    current_shm->tasks[idx].required_resources = requiredResources;
    current_shm->tasks[idx].held_resources = 0;
    current_shm->tasks[idx].assigned_worker_pid = -1;
    current_shm->tasks[idx].wait_ticks = 0;

    /*
     * Увеличиваем количество зарегистрированных задач.
     */
    current_shm->task_count++;


    /*
     * Если задача вообще не требует ресурсов, она сразу может конкурировать за CPU.
     */
    if (requiredResources == 0) {
        queue_push_tail(&current_shm->ready_queue, idx);
        queue_reorder_priority(current_shm, &current_shm->ready_queue);
    } else {
        queue_push_tail(&current_shm->blocked_queue, idx);
    }

    logger_log(LOG_LEVEL_INFO, "Submitted Task #%d base_priority=%d required_resources=0x%X", id, priority, requiredResources);
    log_scheduler_snapshot(current_shm, "TASK SUBMITTED");

    /*
     * Закончили изменение shared memory.
     */
    pthread_mutex_unlock(&current_shm->mutex);

    /*
     * Будим Scheduler.
     */
    sem_post(&current_shm->scheduler_event_sem);
    return id;
}


/*
 * Java:
 *
 * NativeScheduler.changePriority(taskId, priority)
 *
 * Изменяет базовый и эффективный приоритет существующей задачи.
 *
 * При изменении приоритета накопленный aging сбрасывается.
 */
JNIEXPORT void JNICALL
Java_com_taskscheduler_nativebridge_NativeScheduler_changePriority(JNIEnv *env, jobject thiz, jint taskId, jint priority) {
    if (current_shm == NULL) {
        return;
    }

    pthread_mutex_lock(&current_shm->mutex);

    /*
     * Ищем Task по пользовательскому ID.
     */
    for (int i = 0; i < current_shm->task_count; i++) {
        if (current_shm->tasks[i].id == taskId) {
            /*
             * Сохраняем старые значения для лога.
             */
            int old_base = current_shm->tasks[i].base_priority;
            int old_eff = current_shm->tasks[i].effective_priority;

            current_shm->tasks[i].base_priority = priority;
            current_shm->tasks[i].effective_priority = priority;
            current_shm->tasks[i].wait_ticks = 0;


            /*
             * Пересортировка очереди.
             */
            if (queue_contains(&current_shm->ready_queue, i)) {
                queue_reorder_priority(current_shm, &current_shm->ready_queue);
            }

            logger_log(LOG_LEVEL_INFO, "[PRIORITY CHANGE] Task #%d: base %d -> %d, effective %d -> %d", taskId, old_base, priority, old_eff, priority);
            log_scheduler_snapshot(current_shm, "PRIORITY CHANGED");
            break;
        }
    }
    pthread_mutex_unlock(&current_shm->mutex);
    sem_post(&current_shm->scheduler_event_sem);
}


/*
 * Java:
 *
 * NativeScheduler.getTaskCount()
 *
 * Возвращает текущее количество зарегистрированных задач.
 */
JNIEXPORT jint JNICALL
Java_com_taskscheduler_nativebridge_NativeScheduler_getTaskCount(JNIEnv *env, jobject thiz) {
    if (current_shm == NULL) {
        return 0;
    }

    pthread_mutex_lock(&current_shm->mutex);
    int count = current_shm->task_count;
    pthread_mutex_unlock(&current_shm->mutex);
    return count;
}


JNIEXPORT jint JNICALL
Java_com_taskscheduler_nativebridge_NativeScheduler_getTaskState(JNIEnv *env, jobject thiz, jint taskId) {
    if (current_shm == NULL) {
        return -1;
    }

    pthread_mutex_lock(&current_shm->mutex);
    for (int i = 0; i < current_shm->task_count; i++) {
        if (current_shm->tasks[i].id == taskId) {
            int state = current_shm->tasks[i].state;
            pthread_mutex_unlock(&current_shm->mutex);
            return state;
        }
    }
    pthread_mutex_unlock(&current_shm->mutex);
    return -1;
}


JNIEXPORT jlong JNICALL
Java_com_taskscheduler_nativebridge_NativeScheduler_getRemainingTime(JNIEnv *env, jobject thiz, jint taskId) {
    if (current_shm == NULL) {
        return -1;
    }

    pthread_mutex_lock(&current_shm->mutex);
    for (int i = 0; i < current_shm->task_count; i++) {
        if (current_shm->tasks[i].id == taskId) {
            long rem = current_shm->tasks[i].remaining_time_ms;
            pthread_mutex_unlock(&current_shm->mutex);
            return rem;
        }
    }
    pthread_mutex_unlock(&current_shm->mutex);
    return -1;
}


JNIEXPORT jint JNICALL
Java_com_taskscheduler_nativebridge_NativeScheduler_getBasePriority(JNIEnv *env, jobject thiz, jint taskId) {
    if (current_shm == NULL) {
        return -1;
    }

    pthread_mutex_lock(&current_shm->mutex);
    for (int i = 0; i < current_shm->task_count; i++) {
        if (current_shm->tasks[i].id == taskId) {
            int bp = current_shm->tasks[i].base_priority;
            pthread_mutex_unlock(&current_shm->mutex);
            return bp;
        }
    }
    pthread_mutex_unlock(&current_shm->mutex);
    return -1;
}


JNIEXPORT jint JNICALL
Java_com_taskscheduler_nativebridge_NativeScheduler_getEffectivePriority(JNIEnv *env, jobject thiz, jint taskId) {
    if (current_shm == NULL) {
        return -1;
    }

    pthread_mutex_lock(&current_shm->mutex);
    for (int i = 0; i < current_shm->task_count; i++) {
        if (current_shm->tasks[i].id == taskId) {
            int ep = current_shm->tasks[i].effective_priority;
            pthread_mutex_unlock(&current_shm->mutex);
            return ep;
        }
    }
    pthread_mutex_unlock(&current_shm->mutex);
    return -1;
}


JNIEXPORT jint JNICALL
Java_com_taskscheduler_nativebridge_NativeScheduler_getHeldResources(JNIEnv *env, jobject thiz, jint taskId) {
    if (current_shm == NULL) {
        return -1;
    }

    pthread_mutex_lock(&current_shm->mutex);
    for (int i = 0; i < current_shm->task_count; i++) {
        if (current_shm->tasks[i].id == taskId) {
            int held = current_shm->tasks[i].held_resources;
            pthread_mutex_unlock(&current_shm->mutex);
            return held;
        }
    }
    pthread_mutex_unlock(&current_shm->mutex);
    return -1;
}


JNIEXPORT jint JNICALL
Java_com_taskscheduler_nativebridge_NativeScheduler_getRunningTaskId(JNIEnv *env, jobject thiz) {
    if (current_shm == NULL) {
        return -1;
    }

    pthread_mutex_lock(&current_shm->mutex);
    int rid = current_shm->running_task_id;
    pthread_mutex_unlock(&current_shm->mutex);
    return rid;
}


JNIEXPORT jint JNICALL
Java_com_taskscheduler_nativebridge_NativeScheduler_getReadyQueueSize(JNIEnv *env, jobject thiz) {
    if (current_shm == NULL) {
        return 0;
    }

    pthread_mutex_lock(&current_shm->mutex);
    int sz = current_shm->ready_queue.size;
    pthread_mutex_unlock(&current_shm->mutex);
    return sz;
}


JNIEXPORT jint JNICALL
Java_com_taskscheduler_nativebridge_NativeScheduler_getBlockedQueueSize(JNIEnv *env, jobject thiz) {
    if (current_shm == NULL) {
        return 0;
    }

    pthread_mutex_lock(&current_shm->mutex);
    int sz = current_shm->blocked_queue.size;
    pthread_mutex_unlock(&current_shm->mutex);
    return sz;
}


JNIEXPORT jlong JNICALL
Java_com_taskscheduler_nativebridge_NativeScheduler_getWaitTicks(JNIEnv *env, jobject thiz, jint taskId) {
    if (current_shm == NULL) {
        return -1;
    }

    pthread_mutex_lock(&current_shm->mutex);
    for (int i = 0; i < current_shm->task_count; i++) {
        if (current_shm->tasks[i].id == taskId) {
            long wt = current_shm->tasks[i].wait_ticks;
            pthread_mutex_unlock(&current_shm->mutex);
            return wt;
        }
    }
    pthread_mutex_unlock(&current_shm->mutex);
    return -1;
}

