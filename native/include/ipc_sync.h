/*
 * Заголовочный файл для работы с примитивами межпроцессной синхронизации,
 * которые хранятся внутри SharedMemorySegment.
 */

#ifndef IPC_SYNC_H
#define IPC_SYNC_H

#include "scheduler_shm.h"

/*
 * Инициализирует все примитивы синхронизации,
 * расположенные внутри SharedMemorySegment.
 *
 * Возвращает:
 *   0  — успешная инициализация;
 *  -1  — произошла ошибка.
 *
 * Вызывается при первоначальном создании shared memory.
 */
int init_shared_memory_sync(SharedMemorySegment *shm);


/*
 * Уничтожает mutex и semaphore, находящиеся внутри SharedMemorySegment.
 */
void destroy_shared_memory_sync(SharedMemorySegment *shm);

#endif