/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "task.h"
#include "sched.h"
#include "heap.h"
#include "string.h"
#include "io.h"
#include "idt.h"

static task_t task_table[MAX_TASKS];
static uint32_t next_task_id = 1;

static void task_entry_wrapper(task_entry_fn entry, void *arg) {
    sti();

    if (entry) {
        entry(arg);
    }

    rtos_task_exit();
}

task_t *rtos_task_create(const char *name, task_entry_fn entry, void *arg, uint8_t priority, uint32_t stack_size) {
    uint32_t flags;
    task_t *task;
    size_t i;
    uint8_t *stk;
    registers_t *frame;

    if (priority > RTOS_MAX_PRIORITY) {
        priority = RTOS_MAX_PRIORITY;
    }
    if (stack_size == 0) {
        stack_size = DEFAULT_STACK_SIZE;
    }

    flags = irq_save();

    /* Find free TCB slot */
    task = NULL;
    for (i = 0; i < MAX_TASKS; i++) {
        if (task_table[i].state == TASK_STATE_UNUSED || task_table[i].state == TASK_STATE_TERMINATED) {
            task = &task_table[i];
            break;
        }
    }

    if (!task) {
        irq_restore(flags);
        return NULL;
    }

    if (!task->stack_base || task->stack_size < stack_size) {
        if (task->stack_base) {
            kfree(task->stack_base);
        }
        task->stack_base = (uint32_t*)kmalloc_aligned(stack_size, 16);
        if (!task->stack_base) {
            irq_restore(flags);
            return NULL;
        }
        task->stack_size = stack_size;
    }

    task->id = next_task_id++;
    strncpy(task->name, name ? name : "task", sizeof(task->name) - 1);
    task->name[sizeof(task->name) - 1] = '\0';
    task->priority = priority;
    task->base_priority = priority;
    task->sleep_ticks = 0;
    task->runtime_ticks = 0;
    task->wait_object = NULL;
    task->next_wait = NULL;

    /* Prepare initial interrupt stack frame */
    stk = (uint8_t*)task->stack_base + task->stack_size;

    /* Push arguments for task_entry_wrapper (cdecl convention: [esp+8]=arg, [esp+4]=entry, [esp]=ret_addr) */
    stk -= sizeof(void*);
    *(void**)stk = arg;
    stk -= sizeof(void*);
    *(task_entry_fn*)stk = entry;
    stk -= sizeof(void*);
    *(void**)stk = (void*)rtos_task_exit;

    /* Push registers_t structure (56 bytes) */
    stk -= sizeof(registers_t);
    frame = (registers_t*)stk;
    memset(frame, 0, sizeof(registers_t));
    frame->ds = 0x10;
    frame->eip = (uint32_t)task_entry_wrapper;
    frame->cs = 0x08;
    frame->eflags = 0x0202; /* IF=1, Reserved=1 */

    task->esp = (uint32_t*)stk;
    task->state = TASK_STATE_READY;

    irq_restore(flags);

    if (rtos_is_running()) {
        rtos_reschedule();
    }

    return task;
}

void rtos_task_exit(void) {
    uint32_t flags;
    task_t *cur;

    flags = irq_save();
    cur = rtos_current_task();
    if (cur) {
        cur->state = TASK_STATE_TERMINATED;
    }
    irq_restore(flags);
    rtos_reschedule();

    for (;;) { hlt(); }
}

void rtos_task_kill(uint32_t id) {
    uint32_t flags;
    size_t i;

    flags = irq_save();
    for (i = 0; i < MAX_TASKS; i++) {
        if (task_table[i].state != TASK_STATE_UNUSED && task_table[i].id == id) {
            task_table[i].state = TASK_STATE_TERMINATED;
            break;
        }
    }
    irq_restore(flags);
    rtos_reschedule();
}

task_t *rtos_get_task(uint32_t id) {
    size_t i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (task_table[i].state != TASK_STATE_UNUSED && task_table[i].id == id) {
            return &task_table[i];
        }
    }
    return NULL;
}

size_t rtos_get_task_count(void) {
    size_t count;
    size_t i;

    count = 0;
    for (i = 0; i < MAX_TASKS; i++) {
        if (task_table[i].state != TASK_STATE_UNUSED) {
            count++;
        }
    }
    return count;
}

task_t *rtos_get_task_by_index(size_t index) {
    size_t count;
    size_t i;

    count = 0;
    for (i = 0; i < MAX_TASKS; i++) {
        if (task_table[i].state != TASK_STATE_UNUSED) {
            if (count == index) {
                return &task_table[i];
            }
            count++;
        }
    }
    return NULL;
}

const char *rtos_task_state_str(task_state_t state) {
    switch (state) {
        case TASK_STATE_READY:      return "READY";
        case TASK_STATE_RUNNING:    return "RUNNING";
        case TASK_STATE_SLEEPING:   return "SLEEP";
        case TASK_STATE_BLOCKED:    return "BLOCKED";
        case TASK_STATE_TERMINATED: return "TERM";
        default:                    return "UNUSED";
    }
}
