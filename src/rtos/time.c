/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "time.h"
#include "sched.h"
#include "pit.h"
#include "io.h"

void rtos_sleep_ms(uint32_t ms) {
    uint32_t i;
    volatile int d;
    uint32_t ticks;

    if (ms == 0) {
        rtos_yield();
        return;
    }

    if (!rtos_is_running()) {
        /* Pre-scheduler busy wait */
        for (i = 0; i < ms; i++) {
            for (d = 0; d < 10000; d++);
        }
        return;
    }

    ticks = rtos_ms_to_ticks(ms);
    if (ticks == 0) ticks = 1;

    rtos_delay_ticks(ticks);
}

void rtos_delay_ticks(uint32_t ticks) {
    uint32_t i;
    volatile int d;
    uint32_t flags;
    task_t *cur;

    if (ticks == 0) return;

    if (!rtos_is_running()) {
        for (i = 0; i < ticks; i++) {
            for (d = 0; d < 10000; d++);
        }
        return;
    }

    flags = irq_save();
    cur = rtos_current_task();
    if (cur) {
        cur->sleep_ticks = ticks;
        cur->state = TASK_STATE_SLEEPING;
        irq_restore(flags);
        rtos_reschedule();
    } else {
        irq_restore(flags);
        for (i = 0; i < ticks; i++) {
            for (d = 0; d < 10000; d++);
        }
    }
}

uint32_t rtos_get_ticks(void) {
    return pit_get_ticks();
}

uint32_t rtos_ticks_to_ms(uint32_t ticks) {
    return (ticks * 1000) / PIT_FREQUENCY;
}

uint32_t rtos_ms_to_ticks(uint32_t ms) {
    return (ms * PIT_FREQUENCY) / 1000;
}
