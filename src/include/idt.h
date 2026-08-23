/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_IDT_H
#define GEMIOS_IDT_H

#include "types.h"

struct idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} PACKED;

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} PACKED;

/* Ring 0 Interrupt Stack Frame (56 bytes total) */
struct registers {
    uint32_t ds;                                           // Pushed by isr_common_stub (4B)
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax; // Pushed by pusha (32B)
    uint32_t int_no, err_code;                             // Pushed by ISR/IRQ stub (8B)
    uint32_t eip, cs, eflags;                              // Pushed automatically by CPU on interrupt (12B)
};

typedef struct registers registers_t;
typedef void (*isr_handler_t)(registers_t *regs);

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
void register_interrupt_handler(uint8_t n, isr_handler_t handler);

#endif /* GEMIOS_IDT_H */
