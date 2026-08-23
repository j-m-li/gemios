/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_RTOS_TIME_H
#define GEMIOS_RTOS_TIME_H

#include "types.h"

void rtos_sleep_ms(uint32_t ms);
void rtos_delay_ticks(uint32_t ticks);
uint32_t rtos_get_ticks(void);
uint32_t rtos_ticks_to_ms(uint32_t ticks);
uint32_t rtos_ms_to_ticks(uint32_t ms);

#endif /* GEMIOS_RTOS_TIME_H */
