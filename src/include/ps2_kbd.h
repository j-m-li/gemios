/*
 * This is free and unencumbered software released into the public domain.
 * GEMOS Preemptive Real-Time Operating System
 */

#ifndef GEMOS_PS2_KBD_H
#define GEMOS_PS2_KBD_H

#include "types.h"

void ps2_kbd_init(void);
void kbd_push_char(uint16_t key);

#endif /* GEMOS_PS2_KBD_H */
