/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_PIC_H
#define GEMIOS_PIC_H

#include "types.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define PIC_EOI      0x20

#define IRQ_OFFSET   0x20

void pic_init(void);
void pic_send_eoi(uint8_t irq);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);
void pic_disable(void);

#endif /* GEMIOS_PIC_H */
