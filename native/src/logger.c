#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>

static int log_fd = -1;
static char current_component[64] = "GENERAL";

int logger_init(const char *shm_name, const char *component) {
    if (log_fd != -1) {
        close(log_fd);
        log_fd = -1;
    }

    if (component) {
        snprintf(current_component, sizeof(current_component), "%s", component);
    }

    mkdir("logs", 0777);

    const char *p = shm_name;
    while (p && *p == '/') p++;

    char filename[512];
    if (p && *p) {
        snprintf(filename, sizeof(filename), "logs/%s.log", p);
    } else {
        snprintf(filename, sizeof(filename), "logs/default_scheduler.log");
    }

    log_fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
    return log_fd;
}

void logger_close(void) {
    if (log_fd != -1) {
        close(log_fd);
        log_fd = -1;
    }
}

static const char* level_to_str(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "INFO";
    }
}

void logger_log(LogLevel level, const char *format, ...) {
    if (log_fd == -1) {
        // Fallback to stderr if not initialized
        log_fd = open("logs/fallback.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t nowtime = tv.tv_sec;
    struct tm nowtm;
    localtime_r(&nowtime, &nowtm);
    char tmbuf[64];
    strftime(tmbuf, sizeof(tmbuf), "%Y-%m-%d %H:%M:%S", &nowtm);
    int millis = (int)(tv.tv_usec / 1000);

    char msg_buf[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(msg_buf, sizeof(msg_buf), format, args);
    va_end(args);

    char final_buf[4096];
    int len = snprintf(final_buf, sizeof(final_buf), "[%s.%03d][PID=%d][%s][%s] %s\n",
                       tmbuf, millis, (int)getpid(), current_component, level_to_str(level), msg_buf);

    if (len > 0 && log_fd != -1) {
        write(log_fd, final_buf, len);
    }
}

void log_queues(SharedMemorySegment *shm) {
    char rq_buf[1024] = "";
    int offset = 0;
    offset += snprintf(rq_buf + offset, sizeof(rq_buf) - offset, "READY: ");
    if (shm->ready_queue.size == 0) {
        offset += snprintf(rq_buf + offset, sizeof(rq_buf) - offset, "(empty)");
    } else {
        for (int i = 0; i < shm->ready_queue.size; i++) {
            int idx = shm->ready_queue.task_indices[(shm->ready_queue.head + i) % MAX_TASKS];
            Task *t = &shm->tasks[idx];
            offset += snprintf(rq_buf + offset, sizeof(rq_buf) - offset, "#%d(base=%d,eff=%d,wait=%ld)%s",
                               t->id, t->base_priority, t->effective_priority, t->wait_ticks,
                               (i < shm->ready_queue.size - 1) ? " -> " : "");
        }
    }

    offset += snprintf(rq_buf + offset, sizeof(rq_buf) - offset, " | BLOCKED: ");
    if (shm->blocked_queue.size == 0) {
        offset += snprintf(rq_buf + offset, sizeof(rq_buf) - offset, "(empty)");
    } else {
        for (int i = 0; i < shm->blocked_queue.size; i++) {
            int idx = shm->blocked_queue.task_indices[(shm->blocked_queue.head + i) % MAX_TASKS];
            Task *t = &shm->tasks[idx];
            offset += snprintf(rq_buf + offset, sizeof(rq_buf) - offset, "#%d(waiting=0x%X,held=0x%X)%s",
                               t->id, t->required_resources & ~t->held_resources, t->held_resources,
                               (i < shm->blocked_queue.size - 1) ? " -> " : "");
        }
    }

    logger_log(LOG_LEVEL_DEBUG, "%s", rq_buf);
}

void log_scheduler_snapshot(SharedMemorySegment *shm, const char *action_title) {
    logger_log(LOG_LEVEL_INFO, "=== SNAPSHOT: %s ===", action_title ? action_title : "UPDATE");

    // Find running task
    Task *running_t = NULL;
    for (int i = 0; i < shm->task_count; i++) {
        if (shm->tasks[i].id == shm->running_task_id) {
            running_t = &shm->tasks[i];
            break;
        }
    }

    if (running_t) {
        logger_log(LOG_LEVEL_INFO, "  RUNNING: #%d base=%d effective=%d remaining=%ldms",
                   running_t->id, running_t->base_priority, running_t->effective_priority, running_t->remaining_time_ms);
    } else {
        logger_log(LOG_LEVEL_INFO, "  RUNNING: (none)");
    }

    logger_log(LOG_LEVEL_INFO, "  READY QUEUE:");
    if (shm->ready_queue.size == 0) {
        logger_log(LOG_LEVEL_INFO, "    (empty)");
    } else {
        for (int i = 0; i < shm->ready_queue.size; i++) {
            int idx = shm->ready_queue.task_indices[(shm->ready_queue.head + i) % MAX_TASKS];
            Task *t = &shm->tasks[idx];
            logger_log(LOG_LEVEL_INFO, "    %d. #%d base=%d effective=%d wait=%ld remaining=%ldms",
                       i + 1, t->id, t->base_priority, t->effective_priority, t->wait_ticks, t->remaining_time_ms);
        }
    }

    logger_log(LOG_LEVEL_INFO, "  BLOCKED QUEUE:");
    if (shm->blocked_queue.size == 0) {
        logger_log(LOG_LEVEL_INFO, "    (empty)");
    } else {
        for (int i = 0; i < shm->blocked_queue.size; i++) {
            int idx = shm->blocked_queue.task_indices[(shm->blocked_queue.head + i) % MAX_TASKS];
            Task *t = &shm->tasks[idx];
            logger_log(LOG_LEVEL_INFO, "    %d. #%d base=%d effective=%d waiting=0x%X held=0x%X",
                       i + 1, t->id, t->base_priority, t->effective_priority,
                       t->required_resources & ~t->held_resources, t->held_resources);
        }
    }
    logger_log(LOG_LEVEL_INFO, "===============================");
}

