/*
 * Базовая логика работы с очередями задач и механизмом aging.
 */

#include "scheduler_core.h"
#include <stdio.h>


/*
 * Инициализирует пустую кольцевую очередь.
 */
void queue_init(TaskQueue *q) {

    q->size = 0;
    q->head = 0;
    q->tail = 0;

    for (int i = 0; i < MAX_TASKS; i++) {
        q->task_indices[i] = -1;
    }
}


/*
 * Проверяет, пуста ли очередь.
 */
int queue_is_empty(const TaskQueue *q) {
    return q->size == 0;
}


/*
 * Проверяет, находится ли task_idx уже в очереди.
 */
int queue_contains(const TaskQueue *q, int task_idx) {
    for (int i = 0; i < q->size; i++) {
        int idx = (q->head + i) % MAX_TASKS;
        if (q->task_indices[idx] == task_idx) {
            return 1;
        }
    }
    return 0;
}


/*
 * Добавляет задачу в конец очереди.
 */
int queue_push_tail(TaskQueue *q, int task_idx) {

    /*
     * Нельзя добавить больше MAX_TASKS элементов.
     */
    if (q->size >= MAX_TASKS) {
        return -1;
    }

    /*
     * Одна и та же задача не должна находиться в одной очереди несколько раз.
     */
    if (queue_contains(q, task_idx)) {
        return 0;
    }

    /*
     * Помещаем индекс задачи в позицию tail.
     */
    q->task_indices[q->tail] = task_idx;
    q->tail = (q->tail + 1) % MAX_TASKS;
    q->size++;
    return 0;
}


/*
 * Добавляет задачу в начало очереди.
 */
int queue_push_head(TaskQueue *q, int task_idx) {
    if (q->size >= MAX_TASKS) {
        return -1;
    }

    if (queue_contains(q, task_idx)) {
        return 0;
    }


    /*
     * Сначала создаем новую позицию перед текущим head.
     */
    q->head = (q->head - 1 + MAX_TASKS) % MAX_TASKS;
    q->task_indices[q->head] = task_idx;
    q->size++;
    return 0;
}


/*
 * Извлекает и удаляет первую задачу очереди.
 */
int queue_pop_head(TaskQueue *q) {
    if (q->size == 0) {
        return -1;
    }

    /*
     * Получаем индекс первой задачи.
     */
    int task_idx = q->task_indices[q->head];


    /*
     * Освобождаем старую позицию.
     */
    q->task_indices[q->head] = -1;

    /*
     * Сдвигаем начало очереди вперед.
     */
    q->head = (q->head + 1) % MAX_TASKS;
    q->size--;
    return task_idx;
}


/*
 * Удаляет конкретную задачу из произвольного места очереди.
 */
int queue_remove(TaskQueue *q, int task_idx) {
    if (q->size == 0) {
        return 0;
    }

    int found = 0;

    /*
     * Размер сохраняем
     */
    int original_size = q->size;


    /*
     * Временное хранилище для всех задач, кроме удаляемой.
     */
    int temp[MAX_TASKS];
    int temp_size = 0;


    /*
     * Последовательно разбираем всю очередь.
     */
    for (int i = 0; i < original_size; i++) {
        int idx = queue_pop_head(q);
        if (idx == task_idx) {
            found = 1;
        } else {
            temp[temp_size++] = idx;
        }
    }

    /*
     * Восстанавливаем оставшиеся элементы в том же порядке, в котором они находились раньше.
     */
    for (int i = 0; i < temp_size; i++) {
        queue_push_tail(q, temp[i]);
    }

    return found;
}


/*
 * Переупорядочивает очередь по effective_priority.
 * Сам массив tasks[] остается неизменным.
 */
void queue_reorder_priority(SharedMemorySegment *shm, TaskQueue *q) {
    /*
     * Очередь из 0 или 1 элемента уже упорядочена.
     */
    if (q->size <= 1) {
        return;
    }


    /*
     * Временно извлекаем все элементы очереди в обычный массив.
     */
    int elems[MAX_TASKS];
    int count = q->size;
    for (int i = 0; i < count; i++) {
        elems[i] = queue_pop_head(q);
    }

    /*
     * Сортировка
     */
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            Task *t1 = &shm->tasks[elems[j]];
            Task *t2 = &shm->tasks[elems[j + 1]];

            if (t1->effective_priority < t2->effective_priority) {
                int tmp = elems[j];
                elems[j] = elems[j + 1];
                elems[j + 1] = tmp;
            }
        }
    }

    /*
     * После сортировки помещаем индексы обратно в очередь
     */
    for (int i = 0; i < count; i++) {
        queue_push_tail(q, elems[i]);
    }
}


/*
 * Применяет механизм aging к задачам в ready_queue.
 */
void apply_aging(SharedMemorySegment *shm) {

    /*
     * Aging используется только для Priority Scheduling.
     */
    if (shm->active_algorithm != SCHEDULER_ALGORITHM_PRIORITY) {
        return;
    }

    if (shm->ready_queue.size <= 1) {
        return;
    }


    /*
     * Флаг показывает, изменился ли хотя бы один priority.
     */
    int aged = 0;


    /*
     * Проходим по всем задачам, которые сейчас ожидают выполнения в ready_queue.
     */
    for (int i = 0; i < shm->ready_queue.size; i++) {
        int idx = shm->ready_queue.task_indices[(shm->ready_queue.head + i) % MAX_TASKS];
        Task *t = &shm->tasks[idx];

        /*
         * Задача пережила еще один цикл ожидания.
         */
        t->wait_ticks++;

        /*
         * После 5 циклов ожидания увеличиваем effective_priority на 1.
         */
        if (t->wait_ticks >= 5) {
            int old_eff = t->effective_priority;
            t->effective_priority++;

            /*
             * После повышения priority начинаем новый период ожидания.
             */
            t->wait_ticks = 0;
            aged = 1;

            /*
             * Фиксируем изменение в логах.
             */
            logger_log(LOG_LEVEL_WARN, "[AGING] Task #%d: effective_priority %d -> %d", t->id, old_eff, t->effective_priority);
        }
    }

    /*
     * Если effective_priority хотя бы одной задачи изменился,
     * прежний порядок ready_queue может быть уже неправильным.
     */
    if (aged) {

        /*
         * Снова сортируем очередь по effective_priority DESC.
         */
        queue_reorder_priority(shm, &shm->ready_queue);

        logger_log(LOG_LEVEL_INFO, "Ready queue reordered due to aging");

        /*
         * Записываем лог
         */
        log_scheduler_snapshot(shm, "AGING APPLIED");
    }
}
