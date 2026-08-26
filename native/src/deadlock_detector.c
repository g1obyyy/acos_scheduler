#include "scheduler_shm.h"
#include "scheduler_core.h"
#include <stdio.h>
#include <string.h>

static int is_waiting_for(const Task *t_i, const Task *t_j) {
    if (t_i->state != TASK_STATE_BLOCKED || (t_j->state != TASK_STATE_BLOCKED && t_j->state != TASK_STATE_RUNNING)) {
        return 0;
    }
    unsigned int waiting_i = t_i->required_resources & ~t_i->held_resources;
    return (waiting_i & t_j->held_resources) != 0;
}

int detect_and_resolve_deadlocks(SharedMemorySegment *shm) {
    int resolved_count = 0;

    int state_arr[MAX_TASKS];
    memset(state_arr, 0, sizeof(state_arr));

    int cycle_nodes[MAX_TASKS];
    int cycle_len = 0;

    for (int start_idx = 0; start_idx < shm->task_count; start_idx++) {
        if (shm->tasks[start_idx].state != TASK_STATE_BLOCKED) continue;
        if (state_arr[start_idx] != 0) continue;

        int stack[MAX_TASKS];
        int edge_ptr[MAX_TASKS];
        int top = 0;

        stack[0] = start_idx;
        edge_ptr[0] = 0;
        state_arr[start_idx] = 1; // visiting

        while (top >= 0) {
            int u = stack[top];
            int found_next = 0;

            for (int j = edge_ptr[top]; j < shm->task_count; j++) {
                edge_ptr[top] = j + 1;
                Task *t_u = &shm->tasks[u];
                Task *t_v = &shm->tasks[j];

                if (is_waiting_for(t_u, t_v)) {
                    if (state_arr[j] == 1) {
                        cycle_len = 0;
                        int in_cycle = 0;
                        for (int k = 0; k <= top; k++) {
                            if (stack[k] == j) in_cycle = 1;
                            if (in_cycle) {
                                cycle_nodes[cycle_len++] = stack[k];
                            }
                        }
                        found_next = -1;
                        break;
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

            if (found_next == -1) {
                break;
            }

            if (!found_next) {
                state_arr[u] = 2; // visited
                top--;
            }
        }

        if (cycle_len > 0) {
            int best_victim_idx = cycle_nodes[0];
            for (int k = 1; k < cycle_len; k++) {
                Task *curr = &shm->tasks[cycle_nodes[k]];
                Task *best = &shm->tasks[best_victim_idx];

                if (curr->priority < best->priority) {
                    best_victim_idx = cycle_nodes[k];
                } else if (curr->priority == best->priority) {
                    if (curr->remaining_time_ms > best->remaining_time_ms) {
                        best_victim_idx = cycle_nodes[k];
                    } else if (curr->remaining_time_ms == best->remaining_time_ms) {
                        if (curr->id < best->id) {
                            best_victim_idx = cycle_nodes[k];
                        }
                    }
                }
            }

            Task *victim = &shm->tasks[best_victim_idx];
            victim->state = TASK_STATE_DEADLOCK_ABORTED;
            victim->held_resources = 0;
            victim->assigned_worker_pid = 0;
            victim->remaining_time_ms = 0;
            
            queue_remove(&shm->blocked_queue, best_victim_idx);
            queue_remove(&shm->ready_queue, best_victim_idx);

            if (shm->running_task_id == victim->id) {
                shm->running_task_id = -1;
            }
            resolved_count++;
            printf("[DEADLOCK DETECTED] Aborted Task #%d due to circular wait deadlock.\n", victim->id);
            print_queues(shm);

            memset(state_arr, 0, sizeof(state_arr));
            cycle_len = 0;
            start_idx = -1;
        }
    }

    return resolved_count;
}
