/*
 * Реализация управления отдельными процессами Scheduler и Worker.
 */

#include "process_mgmt.h"
#include "scheduler_loop.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>


/*
 * Создает отдельный Scheduler-процесс.
 */
pid_t spawn_scheduler_process(const char *shm_name) {

    /*
     * fork() создает копию текущего процесса.
     */
    pid_t pid = fork();

    /*
     * Не удалось создать Scheduler-процесс.
     */
    if (pid < 0) {
        perror("fork scheduler failed");
        return -1;
    }

    /*
     * Ветка дочернего процесса.
     */
    if (pid == 0) {

        /*
         * Scheduler подключается к указанной shared memory и начинает основной цикл планирования задач.
         */
        run_scheduler_process_loop(shm_name);
        _exit(0);
    }
    return pid;
}


/*
 * Создает отдельный Worker-процесс.
 */
pid_t spawn_worker_process(const char *shm_name) {

    /*
     * Создаем дочерний процесс для Worker.
     */
    pid_t pid = fork();


    /*
     * Ошибка создания процесса.
     */
    if (pid < 0) {
        perror("fork worker failed");
        return -1;
    }


    /*
     * Ветка дочернего процесса.
     */
    if (pid == 0) {

        /*
         * Запускаем основной цикл Worker:
         */
        run_worker_process_loop(shm_name);
        _exit(0);
    }
    return pid;
}


/*
 * Отправляет процессу сигнал SIGTERM.
 */
void terminate_process(pid_t pid) {
    if (pid > 0) {
        kill(pid, SIGTERM);
    }
}


/*
 * Ожидает завершения указанного дочернего процесса.
 */
void wait_for_process(pid_t pid) {
    if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}
