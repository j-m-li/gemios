/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_PS2_KBD_H
#define GEMIOS_PS2_KBD_H

#include "types.h"

void ps2_kbd_init(void);
void kbd_push_char(uint16_t key);

#endif /* GEMIOS_PS2_KBD_H */
