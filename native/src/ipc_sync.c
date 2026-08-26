/*
 * Инициализация и уничтожение примитивов синхронизации,
 * которые находятся внутри SharedMemorySegment и используются
 * сразу несколькими процессами:
 */

#include "ipc_sync.h"
#include <stdio.h>
#include <stdlib.h>


/*
 * Инициализирует все примитивы межпроцессной синхронизации, расположенные внутри SharedMemorySegment.
 */
int init_shared_memory_sync(SharedMemorySegment *shm) {

    /*
     * Атрибуты mutex.
     */
    pthread_mutexattr_t m_attr;


    /*
     * Инициализируем объект атрибутов mutex.
     */
    if (pthread_mutexattr_init(&m_attr) != 0) {
        return -1;
    }

    /*
     * Разрешаем использовать mutex между разными процессами.
     */
    if (pthread_mutexattr_setpshared(&m_attr, PTHREAD_PROCESS_SHARED) != 0) {
        pthread_mutexattr_destroy(&m_attr);
        return -1;
    }


    /*
     * Создаем сам mutex внутри shared memory.
     */
    if (pthread_mutex_init(&shm->mutex, &m_attr) != 0) {
        pthread_mutexattr_destroy(&m_attr);
        return -1;
    }
    pthread_mutexattr_destroy(&m_attr);


    /*
     * Инициализируем semaphore Scheduler -> Worker.
     */
    if (sem_init(&shm->worker_sem, 1, 0) != 0) {
        pthread_mutex_destroy(&shm->mutex);
        return -1;
    }


    /*
     * Инициализируем semaphore Worker -> Scheduler.
     */
    if (sem_init(&shm->scheduler_sem, 1,0) != 0) {
        sem_destroy(&shm->worker_sem);
        pthread_mutex_destroy(&shm->mutex);
        return -1;
    }


    /*
     * Инициализируем semaphore внешних событий Scheduler.
     */
    if (sem_init(&shm->scheduler_event_sem, 1, 0) != 0) {
        sem_destroy(&shm->scheduler_sem);
        sem_destroy(&shm->worker_sem);
        pthread_mutex_destroy(&shm->mutex);
        return -1;
    }

    return 0;
}


/*
 * Уничтожает все примитивы межпроцессной синхронизации, расположенные внутри SharedMemorySegment.
 */
void destroy_shared_memory_sync(SharedMemorySegment *shm) {

    /*
     * Уничтожаем mutex, защищавший общую память.
     */
    pthread_mutex_destroy(&shm->mutex);

    /*
     * Уничтожаем semaphore Scheduler -> Worker.
     */
    sem_destroy(&shm->worker_sem);

    /*
     * Уничтожаем semaphore Worker -> Scheduler.
     */
    sem_destroy(&shm->scheduler_sem);

    /*
     * Уничтожаем semaphore внешних событий Scheduler.
     */
    sem_destroy(&shm->scheduler_event_sem);
}
