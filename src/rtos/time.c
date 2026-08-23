/*
 * This is free and unencumbered software released into the public domain.
 * GEMOS Preemptive Real-Time Operating System
 */

#include "time.h"
#include "sched.h"
#include "pit.h"
#include "io.h"

void rtos_sleep_ms(uint32_t ms) {
    if (ms == 0) {
        rtos_yield();
        return;
    }

    if (!rtos_is_running()) {
        // Pre-scheduler busy wait
        for (uint32_t i = 0; i < ms; i++) {
            for (volatile int d = 0; d < 10000; d++);
        }
        return;
    }

    uint32_t ticks = rtos_ms_to_ticks(ms);
    if (ticks == 0) ticks = 1;

    rtos_delay_ticks(ticks);
}

void rtos_delay_ticks(uint32_t ticks) {
    if (ticks == 0) return;

    if (!rtos_is_running()) {
        for (uint32_t i = 0; i < ticks; i++) {
            for (volatile int d = 0; d < 10000; d++);
        }
        return;
    }

    uint32_t flags = irq_save();
    task_t *cur = rtos_current_task();
    if (cur) {
        cur->sleep_ticks = ticks;
        cur->state = TASK_STATE_SLEEPING;
        irq_restore(flags);
        rtos_reschedule();
    } else {
        irq_restore(flags);
        for (uint32_t i = 0; i < ticks; i++) {
            for (volatile int d = 0; d < 10000; d++);
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
