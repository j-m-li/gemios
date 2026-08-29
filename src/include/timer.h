/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_TIMER_H
#define GEMIOS_TIMER_H

#include "types.h"

#define ACPI_PM_TIMER_FREQ 3579545 /* 3.579545 MHz */

/* Initialize modern hardware timers (ACPI PM-Timer & TSC) */
void timer_init(void);

/* Hardware Timer Availability */
bool acpi_pm_timer_is_available(void);
bool tsc_is_available(void);

/* ACPI PM-Timer APIs */
uint32_t acpi_pm_timer_read(void);
void acpi_pm_timer_delay_ticks(uint32_t ticks);
void acpi_pm_timer_delay_us(uint32_t us);
void acpi_pm_timer_delay_ms(uint32_t ms);

/* TSC APIs */
uint32_t tsc_get_mhz(void);
void tsc_delay_us(uint32_t us);
void tsc_delay_ms(uint32_t ms);

/* Universal Precision Delay APIs (Modern OS style: TSC -> ACPI PM Timer -> Fallback) */
void timer_delay_us(uint32_t us);
void timer_delay_ms(uint32_t ms);

#endif /* GEMIOS_TIMER_H */
