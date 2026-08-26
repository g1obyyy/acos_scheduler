/*
 * Заголовочный файл для управления отдельными процессами Scheduler и Worker.
 */

#ifndef PROCESS_MGMT_H
#define PROCESS_MGMT_H

#include <sys/types.h>

/*
 * Создает отдельный Scheduler-процесс через fork().
 */
pid_t spawn_scheduler_process(const char *shm_name);

/*
 * Создает отдельный Worker-процесс через fork().
 */
pid_t spawn_worker_process(const char *shm_name);

/*
 * Отправляет указанному процессу сигнал SIGTERM.
 */
void terminate_process(pid_t pid);

/*
 * Ожидает завершения конкретного дочернего процесса через waitpid().
 */
void wait_for_process(pid_t pid);

#endif