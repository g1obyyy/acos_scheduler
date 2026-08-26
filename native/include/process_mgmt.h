#ifndef PROCESS_MGMT_H
#define PROCESS_MGMT_H

#include <sys/types.h>

pid_t spawn_scheduler_process(const char *shm_name);
pid_t spawn_worker_process(const char *shm_name);
void terminate_process(pid_t pid);
void wait_for_process(pid_t pid);

#endif