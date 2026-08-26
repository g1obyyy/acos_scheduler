/*
 * Заголовочный файл с основными циклами работы
 * Scheduler и Worker процессов.
 */

#ifndef SCHEDULER_LOOP_H
#define SCHEDULER_LOOP_H

/*
 * Запускает основной цикл Scheduler-процесса.
 *
 * shm_name — имя POSIX Shared Memory, к которой Scheduler
 * подключается для работы с общими задачами и очередями.
 *
 * Функция работает до тех пор, пока в shared memory
 * не будет установлен shutdown_flag.
 */
void run_scheduler_process_loop(const char *shm_name);

/*
 * Запускает основной цикл Worker-процесса.
 *
 * shm_name — имя той же POSIX Shared Memory,
 * которую использует Scheduler.
 *
 * Worker ожидает сигнал от Scheduler, получает выбранную задачу
 * и выполняет ее в течение одного временного кванта.
 */
void run_worker_process_loop(const char *shm_name);

#endif
