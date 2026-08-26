#include "process_mgmt.h"
#include "scheduler_loop.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

pid_t spawn_scheduler_process(const char *shm_name) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork scheduler failed");
        return -1;
    }
    if (pid == 0) {
        run_scheduler_process_loop(shm_name);
        _exit(0);
    }
    return pid;
}

pid_t spawn_worker_process(const char *shm_name) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork worker failed");
        return -1;
    }
    if (pid == 0) {
        run_worker_process_loop(shm_name);
        _exit(0);
    }
    return pid;
}

void terminate_process(pid_t pid) {
    if (pid > 0) {
        kill(pid, SIGTERM);
    }
}

void wait_for_process(pid_t pid) {
    if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}
