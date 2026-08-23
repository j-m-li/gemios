/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_RTOS_TASK_H
#define GEMIOS_RTOS_TASK_H

#include "types.h"

#define MAX_TASKS 32
#define DEFAULT_STACK_SIZE 16384 /* 16 KB */

#define RTOS_PRIORITY_IDLE       0
#define RTOS_PRIORITY_LOW        2
#define RTOS_PRIORITY_BELOW_NORM 4
#define RTOS_PRIORITY_NORMAL     6
#define RTOS_PRIORITY_ABOVE_NORM 8
#define RTOS_PRIORITY_HIGH       10
#define RTOS_PRIORITY_REALTIME   14
#define RTOS_MAX_PRIORITY        15

typedef enum {
    TASK_STATE_UNUSED = 0,
    TASK_STATE_READY,
    TASK_STATE_RUNNING,
    TASK_STATE_SLEEPING,
    TASK_STATE_BLOCKED,
    TASK_STATE_TERMINATED
} task_state_t;

typedef void (*task_entry_fn)(void *arg);

typedef struct task_control_block {
    uint32_t *esp;              /* Saved stack pointer (MUST BE FIRST MEMBER for asm) */
    uint32_t id;                /* Unique task ID */
    char name[32];              /* Task name */
    task_state_t state;         /* Current task state */
    uint8_t priority;           /* Task priority (0..15) */
    uint8_t base_priority;      /* Base priority for priority inheritance */
    uint32_t sleep_ticks;       /* Remaining ticks to sleep */
    uint32_t runtime_ticks;     /* Total CPU time consumed in ticks */
    void *wait_object;          /* Pointer to sync object task is waiting on */
    uint32_t *stack_base;       /* Base address of allocated stack */
    uint32_t stack_size;        /* Size of allocated stack */
    struct task_control_block *next_wait; /* Next task in wait queue */
} task_t;

task_t *rtos_task_create(const char *name, task_entry_fn entry, void *arg, uint8_t priority, uint32_t stack_size);
void rtos_task_exit(void);
void rtos_task_kill(uint32_t id);
task_t *rtos_get_task(uint32_t id);
size_t rtos_get_task_count(void);
task_t *rtos_get_task_by_index(size_t index);
const char *rtos_task_state_str(task_state_t state);

#endif /* GEMIOS_RTOS_TASK_H */
