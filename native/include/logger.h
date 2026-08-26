#ifndef LOGGER_H
#define LOGGER_H

#include "scheduler_shm.h"

typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

int logger_init(const char *shm_name, const char *component);
void logger_close(void);
void logger_log(LogLevel level, const char *format, ...);
void log_queues(SharedMemorySegment *shm);

#endif
