/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "sched.h"
#include "task.h"
#include "io.h"
#include "string.h"

static task_t *current_task = NULL;
static task_t *idle_task = NULL;
static bool scheduler_running = false;
static size_t last_scheduled_idx = 0;

static void idle_task_func(void *arg) {
    UNUSED(arg);
    while (1) {
        rtos_yield();
        sti();
        hlt();
    }
}

void rtos_sched_init(void) {
    scheduler_running = false;
    current_task = NULL;
    last_scheduled_idx = 0;
    idle_task = rtos_task_create("idle", idle_task_func, NULL, RTOS_PRIORITY_IDLE, 4096);
}

bool rtos_is_running(void) {
    return scheduler_running;
}

task_t *rtos_current_task(void) {
    return current_task;
}

static task_t *find_next_ready_task(void) {
    task_t *best_task;
    int highest_priority;
    size_t count;
    size_t start_idx;
    size_t best_idx;
    size_t i;

    best_task = NULL;
    highest_priority = -1;
    count = rtos_get_task_count();
    if (count == 0) return idle_task;

    start_idx = (last_scheduled_idx + 1) % count;
    best_idx = 0;

    for (i = 0; i < count; i++) {
        size_t idx;
        task_t *t;

        idx = (start_idx + i) % count;
        t = rtos_get_task_by_index(idx);
        if (!t) continue;

        if (t->state == TASK_STATE_READY || t->state == TASK_STATE_RUNNING) {
            if ((int)t->priority > highest_priority) {
                highest_priority = t->priority;
                best_task = t;
                best_idx = idx;
            }
            if ((int)t->priority < 15) {
                t->priority++;
            }
        }
    }

    if (best_task) {
        best_task->priority = best_task->base_priority;
        last_scheduled_idx = best_idx;
        return best_task;
    }

    return idle_task;
}

registers_t *rtos_schedule_from_isr(registers_t *regs) {
    task_t *prev;
    task_t *next;

    if (!scheduler_running) return regs;

    /* 1. Update sleeping tasks on timer tick */
    if (regs->int_no == 32) {
        size_t count;
        size_t i;
        count = rtos_get_task_count();
        for (i = 0; i < count; i++) {
            task_t *t = rtos_get_task_by_index(i);
            if (!t) continue;

            if (t->state == TASK_STATE_SLEEPING) {
                if (t->sleep_ticks > 0) {
                    t->sleep_ticks--;
                    if (t->sleep_ticks == 0) {
                        t->state = TASK_STATE_READY;
                    }
                }
            }
        }
    }

    if (current_task) {
        current_task->runtime_ticks++;
    }

    /* 2. Select next highest priority ready task */
    prev = current_task;
    next = find_next_ready_task();
    if (!next) next = idle_task;

    if (prev != next) {
        if (prev) {
            prev->esp = (uint32_t*)regs;
            if (prev->state == TASK_STATE_RUNNING) {
                prev->state = TASK_STATE_READY;
            }
        }

        next->state = TASK_STATE_RUNNING;
        current_task = next;

        return (registers_t*)next->esp;
    }

    return regs;
}

void rtos_yield(void) {
    if (scheduler_running) {
        arch_trigger_yield();
    }
}

void rtos_reschedule(void) {
    rtos_yield();
}

void rtos_task_block(task_t *task, void *wait_obj) {
    if (!task) task = current_task;
    if (!task) return;

    task->state = TASK_STATE_BLOCKED;
    task->wait_object = wait_obj;
    rtos_yield();
}

void rtos_task_unblock(task_t *task) {
    if (!task) return;

    task->state = TASK_STATE_READY;
    task->wait_object = NULL;

    if (current_task && task->priority > current_task->priority) {
        rtos_yield();
    }
}

void rtos_sched_start(void) {
    task_t *first;

    scheduler_running = true;

    first = find_next_ready_task();
    if (!first) first = idle_task;

    first->state = TASK_STATE_RUNNING;
    current_task = first;

    /* Restore context of first task and iret into it */
    rtos_start_first_task(first->esp);

    for (;;) { hlt(); }
}
