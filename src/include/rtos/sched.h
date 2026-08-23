/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_RTOS_SCHED_H
#define GEMIOS_RTOS_SCHED_H

#include "task.h"
#include "idt.h"

void rtos_sched_init(void);
void rtos_sched_start(void) NORETURN;
void rtos_yield(void);
void rtos_reschedule(void);
task_t *rtos_current_task(void);
bool rtos_is_running(void);
void rtos_task_block(task_t *task, void *wait_obj);
void rtos_task_unblock(task_t *task);

/* Preemptive interrupt context switch handler */
registers_t *rtos_schedule_from_isr(registers_t *regs);

#endif /* GEMIOS_RTOS_SCHED_H */
