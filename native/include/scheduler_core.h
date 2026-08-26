/*
 * Заголовочный файл с основными операциями над очередями задач и механизмом aging.
 */

#ifndef SCHEDULER_CORE_H
#define SCHEDULER_CORE_H

#include "scheduler_shm.h"
#include "logger.h"


/*
 * Инициализирует пустую кольцевую очередь.
 */
void queue_init(TaskQueue *q);


/*
 * Проверяет, пуста ли очередь.
 *
 * Возвращает:
 * 1 — очередь пустая;
 * 0 — в очереди есть элементы.
 */
int queue_is_empty(const TaskQueue *q);


/*
 * Добавляет индекс задачи в конец очереди.
 *
 * task_idx — индекс Task внутри массива shm->tasks[],
 *
 * Возвращает:
 *  0 — успешно;
 * -1 — очередь переполнена.
 */
int queue_push_tail(TaskQueue *q, int task_idx);


/*
 * Добавляет индекс задачи в начало очереди.
 */
int queue_push_head(TaskQueue *q, int task_idx);


/*
 * Извлекает и удаляет первый элемент очереди.
 *
 * Возвращает индекс Task из shm->tasks[].
 * Если очередь пуста — возвращает -1.
 */
int queue_pop_head(TaskQueue *q);


/*
 * Удаляет конкретную задачу из очереди.
 *
 * task_idx — индекс задачи в массиве shm->tasks[].
 *
 * Возвращает:
 * 1 — задача найдена и удалена;
 * 0 — задачи в очереди не было.
 */
int queue_remove(TaskQueue *q, int task_idx);


/*
 * Проверяет, находится ли задача в очереди.
 */
int queue_contains(const TaskQueue *q, int task_idx);


/*
 * Переупорядочивает очередь по effective_priority.
 * При одинаковом приоритете сохраняется исходный FIFO-порядок.
 */
void queue_reorder_priority(SharedMemorySegment *shm, TaskQueue *q);


/*
 * Применяет aging к задачам, ожидающим в ready_queue.
 *
 * Долго ожидающая задача постепенно получает увеличение
 * effective_priority, чтобы снизить риск starvation.
 *
 * base_priority при этом не изменяется.
 */
void apply_aging(SharedMemorySegment *shm);


/*
 * Записывает в лог текущее состояние ready_queue и blocked_queue.
 *
 * Реализация находится в logger.c.
 */
void log_queues(SharedMemorySegment *shm);

#endif
