/*
 * Главный заголовочный файл с описанием структур данных,
 * которые размещаются в POSIX Shared Memory и совместно используются:
 */

#ifndef SCHEDULER_SHM_H
#define SCHEDULER_SHM_H

#include <pthread.h>
#include <semaphore.h>

/*
 * Максимальное количество задач, которое может одновременно находиться в системе.
 */
#define MAX_TASKS 256

/*
 * Стандартное имя POSIX Shared Memory
 */
#define SHM_NAME "/task_scheduler_shm"


/*
 * Scheduler всегда выбирает задачу с максимальным effective_priority
 */
typedef enum {
    SCHEDULER_ALGORITHM_PRIORITY = 0
} SchedulerAlgorithm;


/*
 * Возможные состояния задачи.
 *
 * NEW
 *   Задача создана, но еще не готова к выполнению.
 *
 * READY
 *   Задача готова к запуску и находится в ready_queue.
 *
 * RUNNING
 *   Задача в данный момент выполняется Worker-процессом.
 *
 * BLOCKED
 *   Задача не может выполняться, потому что ей не хватает ресурсов.
 *   Такая задача находится в blocked_queue.
 *
 * FINISHED
 *   Задача успешно завершила выполнение.
 *
 * DEADLOCK_ABORTED
 *   Задача была принудительно завершена для разрешения deadlock.
 */
typedef enum {
    TASK_STATE_NEW = 0,
    TASK_STATE_READY = 1,
    TASK_STATE_RUNNING = 2,
    TASK_STATE_BLOCKED = 3,
    TASK_STATE_FINISHED = 4,
    TASK_STATE_DEADLOCK_ABORTED = 5
} TaskStateEnum;


/*
 * Описание одной задачи.
 * Все задачи хранятся в массиве tasks[] внутри SharedMemorySegment
 */
typedef struct {

    /* Уникальный идентификатор задачи. */
    int id;

    /*
     * Базовый приоритет, заданный пользователем.
     */
    int base_priority;

    /*
     * Текущий эффективный приоритет.
     */
    int effective_priority;

    /*
     * Текущее состояние задачи.
     */
    int state;

    /* 
     * Полное требуемое время выполнения задачи в миллисекундах. 
     */
    long total_time_ms;

    /*
     * Оставшееся время выполнения.
     */
    long remaining_time_ms;

    /*
     * Битовая маска всех ресурсов, необходимых задаче.
     */
    unsigned int required_resources;

    /*
     * Битовая маска ресурсов, которыми задача уже владеет.
     */
    unsigned int held_resources;

    /*
     * PID Worker-процесса, который сейчас выполняет задачу.
     */
    int assigned_worker_pid;

    /*
     * Счетчик ожидания задачи в ready_queue.
     */
    long wait_ticks;

} Task;


/*
 * Кольцевая очередь задач.
 * task_indices[] хранит не сами Task и не указатели на Task, а индексы элементов массива
 */
typedef struct {

    /* 
     * Индексы задач из массива tasks[]. 
     */
    int task_indices[MAX_TASKS];

    /* 
     * Текущее количество элементов в очереди. 
     */
    int size;

    /*
     * Позиция первого элемента.
     */
    int head;

    /*
     * Позиция для добавления нового элемента в конец очереди.
     */
    int tail;

} TaskQueue;


/*
 * Основная структура разделяемой памяти.
 */
typedef struct {

    /*
     * Хранилище всех задач.
     */
    Task tasks[MAX_TASKS];

    /* 
     * Количество созданных задач в массиве tasks[]. 
     */
    int task_count;


    /*
     * Очередь задач, готовых к выполнению.
     */
    TaskQueue ready_queue;


    /*
     * Очередь задач, которым пока не хватает ресурсов.
     */
    TaskQueue blocked_queue;


    /*
     * ID задачи, которую Scheduler выбрал для Worker.
     */
    int running_task_id;


    /*
     * Межпроцессный mutex для защиты общей памяти.
     */
    pthread_mutex_t mutex;


    /*
     * Семафор Scheduler -> Worker.
     */
    sem_t worker_sem;


    /*
     * Семафор Worker -> Scheduler.
     */
    sem_t scheduler_sem;


    /*
     * Семафор внешних событий Scheduler.
     */
    sem_t scheduler_event_sem;


    /*
     * Флаг завершения работы.
     *
     * 0 — Scheduler и Worker продолжают выполнение.
     * 1 — процессы должны завершить свои циклы.
     */
    int shutdown_flag;


    /*
     * Активный алгоритм планирования.
     */
    int active_algorithm;


    /*
     * Размер временного кванта Worker в миллисекундах.
     */
    long time_quantum_ms;

} SharedMemorySegment;


/*
 * Создает новый или открывает существующий POSIX Shared Memory object и отображает его в адресное пространство процесса.
 *
 * name       — имя shared memory;
 * is_creator — является ли текущий процесс владельцем ее жизненного цикла.
 */
SharedMemorySegment* create_or_open_shm(
    const char *name,
    int is_creator
);


/*
 * Инициализирует только что созданный SharedMemorySegment:
 * начальные значения полей, mutex, semaphore и другое общее состояние.
 */
void init_shm_content(
    SharedMemorySegment *shm
);


/*
 * Отключает процесс от shared memory.
 */
void close_and_unlink_shm(
    const char *name,
    SharedMemorySegment *shm,
    int is_creator
);


/*
 * Ищет циклическое ожидание ресурсов между задачами и при необходимости разрешает deadlock,
 * принудительно завершая одну из задач.
 *
 * Возвращает количество разрешенных deadlock-ситуаций.
 */
int detect_and_resolve_deadlocks(
    SharedMemorySegment *shm
);

#endif