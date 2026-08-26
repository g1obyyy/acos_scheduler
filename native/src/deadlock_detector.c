/*
 * Реализация обнаружения и разрешения deadlock между задачами.
 */

#include "scheduler_shm.h"
#include "scheduler_core.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>


/*
 * Определяет:
 * ждет ли задача t_i какой-либо ресурс,
 * который сейчас удерживает задача t_j.
 */
static int is_waiting_for(const Task *t_i, const Task *t_j) {

    /*
     * Ребро может исходить только от BLOCKED-задачи
     */
    if (t_i->state != TASK_STATE_BLOCKED || (t_j->state != TASK_STATE_BLOCKED && t_j->state != TASK_STATE_RUNNING)) {
        return 0;
    }

    /*
     * Вычисляем ресурсы, которых t_i еще не хватает
     */
    unsigned int waiting_i = t_i->required_resources & ~t_i->held_resources;

    /*
     * Проверяем пересечение
     */
    return (waiting_i & t_j->held_resources) != 0;
}


/*
 * Ищет deadlock в системе и при необходимости разрешает его.
 *
 * Для обнаружения цикла используется обход wait-for graph
 * в глубину — DFS.
 *
 * Если найден цикл, выбирается задача-жертва и переводится
 * в состояние DEADLOCK_ABORTED.
 *
 * Возвращает количество разрешенных deadlock-ситуаций.
 */
int detect_and_resolve_deadlocks(SharedMemorySegment *shm) {

    /*
     * Сколько deadlock-ситуаций было разрешено за один вызов функции.
     */
    int resolved_count = 0;


    /*
     * Состояние каждой вершины при DFS.
     *
     * Значения:
     *
     * 0 — задача еще не посещалась;
     * 1 — задача сейчас находится в текущем DFS-пути;
     * 2 — задача полностью обработана.
     */
    int state_arr[MAX_TASKS];

    memset(state_arr, 0, sizeof(state_arr));

    /*
     * Здесь сохраняются индексы задач, которые входят в найденный цикл.
     */
    int cycle_nodes[MAX_TASKS];
    int cycle_len = 0;

    /*
     * Поочередно рассматриваем BLOCKED-задачи как возможные стартовые вершины DFS.
     */
    for (int start_idx = 0; start_idx < shm->task_count; start_idx++) {

        /*
         * Только BLOCKED-задача может ожидать чужой ресурс.
         */
        if (shm->tasks[start_idx].state != TASK_STATE_BLOCKED) {
            continue;
        }

        if (state_arr[start_idx] != 0) {
            continue;
        }

        int stack[MAX_TASKS];
        int edge_ptr[MAX_TASKS];
        int top = 0;


        /*
         * Помещаем стартовую вершину в стек.
         */
        stack[0] = start_idx;

        /*
         * Для нее еще не проверяли ни одной потенциальной связи.
         */
        edge_ptr[0] = 0;


        /*
         * Помечаем вершину как находящуюся в текущем пути DFS.
         */
        state_arr[start_idx] = 1;


        /*
         * Пока стек не пуст, продолжаем обход wait-for graph.
         */
        while (top >= 0) {

            /*
             * Текущая вершина DFS.
             */
            int u = stack[top];
            int found_next = 0;


            /*
             * Ищем задачу, от которой зависит текущая Task u.
             */
            for (int j = edge_ptr[top]; j < shm->task_count; j++) {

                /*
                 * Запоминаем, с какого места продолжать
                 * обход соседей при возврате к вершине u.
                 */
                edge_ptr[top] = j + 1;

                Task *t_u = &shm->tasks[u];
                Task *t_v = &shm->tasks[j];

                /*
                 * Если t_u ждет ресурс, который удерживает t_v,
                 * значит в wait-for graph существует ребро:
                 *
                 * u -> j
                 */
                if (is_waiting_for(t_u, t_v)) {

                    /*
                     * Если вершина j уже находится в текущем DFS-пути, мы нашли обратное ребро.
                     */
                    if (state_arr[j] == 1) {
                        cycle_len = 0;
                        int in_cycle = 0;

                        for (int k = 0; k <= top; k++) {
                            if (stack[k] == j) {
                                in_cycle = 1;
                            }

                            if (in_cycle) {
                                cycle_nodes[cycle_len++] = stack[k];
                            }
                        }

                        found_next = -1;
                        break;

                    /*
                     * Если сосед еще вообще не посещался, добавляем его в стек и продолжаем DFS глубже.
                     */
                    } else if (state_arr[j] == 0) {
                        state_arr[j] = 1;
                        top++;
                        stack[top] = j;
                        edge_ptr[top] = 0;
                        found_next = 1;
                        break;
                    }
                }
            }


            /*
             * Если цикл обнаружен, прекращаем текущий DFS.
             */
            if (found_next == -1) {
                break;
            }

            /*
             * Если новых соседей у текущей вершины больше нет,
             * помечаем ее как полностью обработанную
             * и возвращаемся на уровень выше.
             */
            if (!found_next) {
                state_arr[u] = 2;
                top--;
            }
        }

        if (cycle_len > 0) {

            /*
             * Нужно выбрать одну задачу-жертву,
             */
            int best_victim_idx = cycle_nodes[0];
            for (int k = 1; k < cycle_len; k++) {
                Task *curr = &shm->tasks[cycle_nodes[k]];
                Task *best = &shm->tasks[best_victim_idx];

                /*
                 * жертвой становится задача с меньшим effective_priority.
                 */
                if (curr->effective_priority < best->effective_priority) {
                    best_victim_idx = cycle_nodes[k];

                /*
                 * Если приоритеты одинаковые.
                 */
                } else if (curr->effective_priority == best->effective_priority) {

                    /*
                     * Предпочитаем прервать задачу, которой осталось больше времени выполнения.
                     */
                    if (curr->remaining_time_ms > best->remaining_time_ms) {
                        best_victim_idx = cycle_nodes[k];

                    /*
                     * Если remaining_time тоже одинаковое, используем ID.
                     */
                    } else if (curr->remaining_time_ms == best->remaining_time_ms) {
                        if (curr->id < best->id) {
                            best_victim_idx = cycle_nodes[k];
                        }
                    }
                }
            }


            /*
             * Получаем выбранную задачу-жертву.
             */
            Task *victim = &shm->tasks[best_victim_idx];
            victim->state = TASK_STATE_DEADLOCK_ABORTED;


            /*
             * Освобождаем все ресурсы жертвы.
             */
            victim->held_resources = 0;


            /*
             * Задача больше не закреплена за Worker.
             */
            victim->assigned_worker_pid = 0;
            victim->remaining_time_ms = 0;


            /*
             * Удаляем задачу из всех рабочих очередей.
             */
            queue_remove(&shm->blocked_queue, best_victim_idx);
            queue_remove(&shm->ready_queue, best_victim_idx);

            if (shm->running_task_id == victim->id) {
                shm->running_task_id = -1;
            }


            /*
             * Учитываем разрешенный deadlock.
             */
            resolved_count++;


            /*
             * Записываем информацию о deadlock в лог.
             */
            logger_log(LOG_LEVEL_WARN, "Deadlock detected. Victim Task #%d aborted", victim->id);
            log_queues(shm);

            memset(state_arr, 0, sizeof(state_arr));
            cycle_len = 0;
            start_idx = -1;
        }
    }

    return resolved_count;
}
