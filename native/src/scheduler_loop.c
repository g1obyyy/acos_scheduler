/*
 * Основная логика двух native-процессов системы:
 */

#include "scheduler_loop.h"
#include "scheduler_shm.h"
#include "scheduler_core.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>


/*
 * Безопасный вариант sem_wait().
 */
static int safe_sem_wait(sem_t *sem) {
    int res;

    do {
        res = sem_wait(sem);
    } while (
        res == -1 && errno == EINTR
    );

    return res;
}


/*
 * Основной цикл Scheduler-процесса.
 * После завершения Worker одного quantum
 * Scheduler снова получает управление.
 */
void run_scheduler_process_loop(const char *shm_name) {

    /*
     * Каждый процесс самостоятельно инициализирует logger.
     * Все сообщения этого процесса будут иметь компонент:
     * [SCHEDULER]
     */
    logger_init(shm_name, "SCHEDULER");
    logger_log(LOG_LEVEL_INFO, "Scheduler process loop started (PID: %d, shm: %s)", (int)getpid(), shm_name);

    /*
     * Scheduler не создает Shared Memory заново.
     */
    int fd = shm_open(shm_name, O_RDWR, 0666);

    /*
     * Если shared memory открыть не удалось,
     * Scheduler не может продолжать работу.
     */
    if (fd == -1) {
        logger_log(LOG_LEVEL_ERROR, "Failed to shm_open('%s'): errno=%d (%s)", shm_name, errno, strerror(errno));
        logger_close();
        return;
    }

    /*
     * Отображаем Shared Memory в адресное пространство Scheduler.
     */
    SharedMemorySegment *shm = mmap(NULL, sizeof(SharedMemorySegment), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        logger_log(LOG_LEVEL_ERROR, "Failed to mmap('%s'): errno=%d (%s)", shm_name, errno, strerror(errno));
        close(fd);
        logger_close();
        return;
    }

    /*
     * Главный цикл Scheduler.
     */
    while (!shm->shutdown_flag) {
        pthread_mutex_lock(&shm->mutex);

        /*
         * Показывает, произошло ли какое-либо полезное изменение
         * при распределении ресурсов / разрешении deadlock.
         *
         * Если progress был, Scheduler может сразу выполнить
         * еще один цикл, не засыпая на scheduler_event_sem.
         */
        int allocation_progress = 0;


        /*
         * Определяем все ресурсы, которые уже заняты задачами.
         */
        unsigned int globally_held_resources = 0;

        for (int i = 0; i < shm->task_count; i++) {
            Task *t = &shm->tasks[i];

            /*
             * FINISHED и DEADLOCK_ABORTED задачи больше не должны удерживать ресурсы.
             */
            if (t->state == TASK_STATE_DEADLOCK_ABORTED || t->state == TASK_STATE_FINISHED) {
                t->held_resources = 0;
                continue;
            }
            globally_held_resources |= t->held_resources;
        }

        /*
         * Обработка blocked_queue.
         */
        int blocked_count = shm->blocked_queue.size;

        for (int i = 0; i < blocked_count; i++) {
            int task_idx = queue_pop_head(&shm->blocked_queue);

            if (task_idx == -1) {
                continue;
            }

            Task *t = &shm->tasks[task_idx];

            /*
             * Терминальные задачи не должны участвовать
             * в распределении ресурсов.
             */
            if (t->state == TASK_STATE_DEADLOCK_ABORTED || t->state == TASK_STATE_FINISHED) {
                t->held_resources = 0;
                continue;
            }


            /*
             * Определяем ресурсы, которых задаче еще не хватает.
             */
            unsigned int needed = t->required_resources & ~t->held_resources;
            if (needed == 0) {
                t->state = TASK_STATE_READY;
                queue_push_tail(&shm->ready_queue, task_idx);
                queue_reorder_priority(shm, &shm->ready_queue);
                allocation_progress = 1;

                logger_log(LOG_LEVEL_INFO, "Task #%d transitioned BLOCKED -> READY (all resources acquired)", t->id);
                log_scheduler_snapshot(shm, "BLOCKED -> READY");
            } else {

                /*
                 * Пытаемся выдать ОДИН свободный ресурс.
                 */
                for (int bit = 0; bit < 32; bit++) {
                    unsigned int res = (1U << bit);
                    if ((needed & res) != 0) {

                        /*
                         * Проверяем: не занят ли этот ресурс другой задачей.
                         */
                        if ((res & globally_held_resources) == 0) {

                            /*
                             * Передаем ресурс задаче.
                             */
                            t->held_resources |= res;
                            globally_held_resources |= res;
                            allocation_progress = 1;

                            logger_log(LOG_LEVEL_INFO, "Task #%d acquired resource 0x%X (held: 0x%X)", t->id, res, t->held_resources);
                            break;
                        }
                    }
                }

                /*
                 * Если после выдачи ресурса Task получила полный набор required_resources:
                 */
                if (t->held_resources == t->required_resources) {
                    t->state = TASK_STATE_READY;
                    queue_push_tail(&shm->ready_queue, task_idx);
                    queue_reorder_priority(shm, &shm->ready_queue);
                    logger_log(LOG_LEVEL_INFO, "Task #%d transitioned BLOCKED -> READY (gradual allocation completed)", t->id);
                    log_scheduler_snapshot(shm, "BLOCKED -> READY (Gradual)");
                } else {

                    /*
                     * Ресурсов все еще не хватает.
                     */
                    t->state = TASK_STATE_BLOCKED;
                    queue_push_tail(&shm->blocked_queue, task_idx);
                }
            }
        }


        /*
         * Поиск и разрешение deadlock.
         */
        int resolved = detect_and_resolve_deadlocks(shm);
        if (resolved > 0) {

            /*
             * После deadlock resolution были освобождены ресурсы,
             * поэтому имеет смысл сразу выполнить следующий цикл.
             */
            allocation_progress = 1;
            log_scheduler_snapshot(shm, "DEADLOCK RESOLVED");
        }


        /*
         * Повторно вычисляем занятые ресурсы.
         */
        globally_held_resources = 0;
        for (int i = 0; i < shm->task_count; i++) {
            Task *t = &shm->tasks[i];
            if (t->state == TASK_STATE_DEADLOCK_ABORTED || t->state == TASK_STATE_FINISHED) {
                t->held_resources = 0;
                continue;
            }
            globally_held_resources |= t->held_resources;
        }

        /*
         * Aging.
         */
        apply_aging(shm);


        /*
         * Выбор задачи из ready_queue.
         */
        int task_idx = -1;
        if (!queue_is_empty(&shm->ready_queue)) {
            task_idx = queue_pop_head(&shm->ready_queue);
        }

        /*
         * Если READY-задача найдена готовим ее для передачи Worker.
         */
        if (task_idx != -1) {
            Task *t = &shm->tasks[task_idx];

            t->wait_ticks = 0;
            t->effective_priority = t->base_priority;

            shm->running_task_id = t->id;

            logger_log(LOG_LEVEL_INFO, "[SCHEDULER] Selected Task #%d base=%d effective=%d", t->id, t->base_priority, t->effective_priority);
            log_scheduler_snapshot(shm, "TASK SELECTED");

            pthread_mutex_unlock(&shm->mutex);


            /*
             * Scheduler -> Worker:
             */
            sem_post(&shm->worker_sem);

            /*
             * Scheduler ожидает, пока Worker завершит один quantum.
             */
            safe_sem_wait(&shm->scheduler_sem);


            /*
             * После пробуждения проверяем, не был ли одновременно запрошен shutdown.
             */
            if (shm->shutdown_flag) {
                break;
            }
        } else {

            /*
             * READY-задач нет.
             */
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
    logger_log(LOG_LEVEL_INFO,"Scheduler process loop shutting down");
    logger_close();

    /*
     * Отключаем отображение shared memory от Scheduler-процесса.
     */
    munmap(shm, sizeof(SharedMemorySegment));
    close(fd);
}


/*
 * Основной цикл Worker'а
 */
void run_worker_process_loop(const char *shm_name) {

    /*
     * Worker имеет собственный logger descriptor
     */
    logger_init(shm_name,"WORKER");

    logger_log(LOG_LEVEL_INFO, "Worker process loop started (PID: %d, shm: %s)", (int)getpid(), shm_name);


    /*
     * Подключаемся к shared memory.
     */
    int fd = shm_open(shm_name, O_RDWR, 0666);
    if (fd == -1) {
        logger_log(LOG_LEVEL_ERROR, "Failed to shm_open('%s'): errno=%d (%s)", shm_name, errno, strerror(errno));
        logger_close();
        return;
    }

    /*
     * Отображаем SharedMemorySegment в память Worker.
     */
    SharedMemorySegment *shm = mmap(NULL, sizeof(SharedMemorySegment), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        logger_log(LOG_LEVEL_ERROR, "Failed to mmap('%s'): errno=%d (%s)", shm_name, errno, strerror(errno));
        close(fd);
        logger_close();
        return;
    }

    /*
     * Основной цикл Worker.
     */
    while (!shm->shutdown_flag) {
        if (safe_sem_wait(&shm->worker_sem) == -1) {
            if (shm->shutdown_flag) {
                break;
            }
            continue;
        }

        /*
         * Semaphore также мог быть поднят только для shutdown.
         */
        if (shm->shutdown_flag) {
            break;
        }

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

        /*
         * Если назначенная Scheduler задача существует.
         */
        if (current_task) {

            /*
             * Scheduler должен назначать Worker только READY-задачу.
             */
            if (current_task->state != TASK_STATE_READY) {
                logger_log(LOG_LEVEL_ERROR, "Invalid task state %d for assigned task ID %d", current_task->state, current_task->id);
                shm->running_task_id = -1;
                current_task->assigned_worker_pid = 0;
                pthread_mutex_unlock(&shm->mutex);

                /*
                 * Сообщаем Scheduler, что Worker завершил обработку назначения.
                 */
                sem_post(&shm->scheduler_sem);
                sem_post(&shm->scheduler_event_sem);
                continue;
            }

            if (current_task->remaining_time_ms <= 0) {
                current_task->state = TASK_STATE_FINISHED;

                /*
                 * Завершенная Task освобождает ресурсы.
                 */
                current_task->held_resources = 0;
                shm->running_task_id = -1;
                current_task->assigned_worker_pid = 0;
                logger_log(LOG_LEVEL_INFO, "[WORKER] Task #%d FINISHED", current_task->id);
                log_scheduler_snapshot(shm, "TASK FINISHED");
                
                pthread_mutex_unlock(&shm->mutex);


                /*
                 * Worker -> Scheduler:
                 */
                sem_post(&shm->scheduler_sem);
                sem_post(&shm->scheduler_event_sem);
                continue;
            }

            /*
             * READY -> RUNNING
             */
            current_task->state = TASK_STATE_RUNNING;

            /*
             * Сохраняем PID Worker, который сейчас выполняет задачу.
             */
            current_task->assigned_worker_pid = getpid();

            /*
             * Определяем размер временного кванта.
             */
            long quantum = shm->time_quantum_ms > 0 ? shm->time_quantum_ms : 50;
            long exec_time = current_task->remaining_time_ms < quantum ? current_task->remaining_time_ms : quantum;

            logger_log(LOG_LEVEL_INFO, "[WORKER] Task #%d RUNNING quantum=%ldms remaining=%ldms", current_task->id, exec_time, current_task->remaining_time_ms);

            pthread_mutex_unlock(&shm->mutex);

            /*
             * Имитация реального выполнения Task.
             */
            usleep(exec_time * 1000);

            pthread_mutex_lock(&shm->mutex);

            /*
             * Уменьшаем оставшееся время выполнения.
             */
            current_task->remaining_time_ms -= exec_time;


            /*
             * задача полностью выполнена.
             */
            if (current_task->remaining_time_ms <= 0) {
                current_task->state = TASK_STATE_FINISHED;

                /*
                 * FINISHED Task освобождает все свои ресурсы.
                 */
                current_task->held_resources = 0;
                shm->running_task_id = -1;
                current_task->assigned_worker_pid = 0;
                logger_log(LOG_LEVEL_INFO, "[WORKER] Task #%d FINISHED", current_task->id);
                log_scheduler_snapshot(shm, "TASK FINISHED");
            } else {

                /*
                 * Task еще не завершена.
                 *
                 * RUNNING -> READY
                 */
                current_task->state = TASK_STATE_READY;

                /*
                 * Возвращаем индекс Task в ready_queue.
                 */
                queue_push_tail(&shm->ready_queue, current_task_idx);
                queue_reorder_priority(shm, &shm->ready_queue);
                shm->running_task_id = -1;
                current_task->assigned_worker_pid = 0;


                logger_log(LOG_LEVEL_INFO, "[WORKER] Task #%d PREEMPTED remaining=%ldms. Re-ordered in ready_queue", current_task->id, current_task->remaining_time_ms);
                log_scheduler_snapshot(shm, "TASK PREEMPTED");
            }
        } else {

            /*
             * running_task_id содержит ID, которого нет среди зарегистрированных tasks[].
             */
            logger_log(LOG_LEVEL_ERROR, "running_task_id %d does not match any existing task", running_id);
            shm->running_task_id = -1;
        }

        pthread_mutex_unlock(&shm->mutex);
        /*
         * Worker -> Scheduler:
         */
        sem_post(&shm->scheduler_sem);
    }


    logger_log(LOG_LEVEL_INFO, "Worker process loop shutting down");
    logger_close();

    /*
     * Worker только отключается от shared memory.
     */
    munmap(shm, sizeof(SharedMemorySegment));
    close(fd);
}
