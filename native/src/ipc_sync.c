#include "ipc_sync.h"
#include <stdio.h>
#include <stdlib.h>

int init_shared_memory_sync(SharedMemorySegment *shm) {
    pthread_mutexattr_t m_attr;

    if (pthread_mutexattr_init(&m_attr) != 0) return -1;
    if (pthread_mutexattr_setpshared(&m_attr, PTHREAD_PROCESS_SHARED) != 0) {
        pthread_mutexattr_destroy(&m_attr);
        return -1;
    }
    if (pthread_mutex_init(&shm->mutex, &m_attr) != 0) {
        pthread_mutexattr_destroy(&m_attr);
        return -1;
    }
    pthread_mutexattr_destroy(&m_attr);

    if (sem_init(&shm->worker_sem, 1, 0) != 0) {
        pthread_mutex_destroy(&shm->mutex);
        return -1;
    }
    if (sem_init(&shm->scheduler_sem, 1, 0) != 0) {
        sem_destroy(&shm->worker_sem);
        pthread_mutex_destroy(&shm->mutex);
        return -1;
    }
    if (sem_init(&shm->scheduler_event_sem, 1, 0) != 0) {
        sem_destroy(&shm->scheduler_sem);
        sem_destroy(&shm->worker_sem);
        pthread_mutex_destroy(&shm->mutex);
        return -1;
    }

    return 0;
}

void destroy_shared_memory_sync(SharedMemorySegment *shm) {
    pthread_mutex_destroy(&shm->mutex);
    sem_destroy(&shm->worker_sem);
    sem_destroy(&shm->scheduler_sem);
    sem_destroy(&shm->scheduler_event_sem);
}
