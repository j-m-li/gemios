/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_PIT_H
#define GEMIOS_PIT_H

#include "types.h"

#define PIT_FREQUENCY 1000 /* 1000 Hz = 1ms tick */

void pit_init(uint32_t frequency);
uint32_t pit_get_ticks(void);
uint32_t pit_get_uptime_sec(void);
uint32_t pit_get_uptime_ms(void);

#endif /* GEMIOS_PIT_H */
