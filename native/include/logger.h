/*
 * Заголовочный файл подсистемы логирования.
 */

#ifndef LOGGER_H
#define LOGGER_H

#include "scheduler_shm.h"


/*
 * Уровни логирования.
 *
 * DEBUG
 *   Подробная техническая информация:
 *   состояние очередей, внутренние переходы, диагностические данные.
 *
 * INFO
 *   Основные нормальные события работы системы:
 *   запуск процессов, выбор задачи, выполнение quantum, завершение задачи.
 *
 * WARN
 *   Потенциально проблемные, но не критические ситуации:
 *   aging, deadlock.
 *
 * ERROR
 *   Ошибки, из-за которых часть системы не может
 *   корректно продолжить работу:
 *   ошибки shared memory, mmap, semaphore.
 */
typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;


/*
 * Инициализирует logger для текущего процесса.
 *
 * shm_name:
 *   имя shared memory, связанной с конкретным экземпляром Scheduler.
 *   Обычно используется также для формирования имени log-файла.
 *
 * component:
 *   имя компонента, который будет отображаться в каждой строке лога.
 *
 * Возвращает:
 *   0  — успешная инициализация;
 *  -1  — logger не удалось создать/открыть.
 */
int logger_init(const char *shm_name, const char *component);


/*
 * Закрывает logger текущего процесса.
 */
void logger_close(void);


/*
 * Записывает одну строку в log-файл.
 *
 * level:
 *   уровень сообщения.
 *
 * format:
 *   строка форматирования в стиле printf().
 *
 * Дополнительные аргументы передаются через (...)
 */
void logger_log(LogLevel level, const char *format, ...);


/*
 * Записывает в лог текущее состояние очередей.
 *
 */
void log_queues(SharedMemorySegment *shm);


/*
 * Записывает более полный snapshot состояния Scheduler.
 *
 * action_title:
 *   краткое название события, после которого снимается snapshot.
 */
void log_scheduler_snapshot(SharedMemorySegment *shm, const char *action_title);

#endif
