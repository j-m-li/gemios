/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_APIC_H
#define GEMIOS_APIC_H 1

#include "types.h"

#define APIC_TIMER_FREQUENCY 1000 /* 1000 Hz = 1ms tick */
#define APIC_TIMER_VECTOR    32   /* IRQ0 vector */

bool apic_init(void);
bool apic_is_active(void);
uintptr_t apic_get_base(void);
void apic_send_eoi(void);

#endif /* GEMIOS_APIC_H */
