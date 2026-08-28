/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "pit.h"
#include "idt.h"
#include "pic.h"
#include "io.h"

#define PIT_CH0_DATA 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_FREQ 1193182

static volatile uint32_t pit_ticks = 0;
static uint32_t current_freq = 1000;

void pit_tick(void) {
    pit_ticks++;
}

static void pit_irq_handler(registers_t *regs) {
    UNUSED(regs);
    /* Handled centrally in isr_handler */
}

void pit_init(uint32_t frequency) {
    uint32_t divisor;
    current_freq = frequency;
    divisor = PIT_BASE_FREQ / frequency;

    if (divisor == 0) divisor = 1;
    if (divisor > 65535) divisor = 65535;

    /* Channel 0, lobyte/hibyte, mode 3 (square wave), binary */
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CH0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0_DATA, (uint8_t)((divisor >> 8) & 0xFF));

    register_interrupt_handler(32, pit_irq_handler);
    pic_unmask_irq(0); /* Unmask IRQ0 */
}

uint32_t pit_get_ticks(void) {
    return pit_ticks;
}

uint32_t pit_get_uptime_sec(void) {
    return pit_ticks / current_freq;
}

uint32_t pit_get_uptime_ms(void) {
    return (pit_ticks * 1000) / current_freq;
}

/* Hardware-timed delay using PIT Channel 2 counter */
void pit_delay_ms(uint32_t ms) {
    uint32_t i;
    uint8_t status;

    for (i = 0; i < ms; i++) {
        /* Enable Channel 2 gate (bit 0 = 1), speaker off (bit 1 = 0) */
        status = inb(0x61);
        outb(0x61, (status & 0xFD) | 0x01);

        /* Channel 2, Mode 0 (Interrupt on terminal count), binary 16-bit */
        outb(PIT_COMMAND, 0xB0);
        outb(0x42, (uint8_t)(1193 & 0xFF));
        outb(0x42, (uint8_t)((1193 >> 8) & 0xFF));

        /* Wait for Channel 2 output (bit 5 of port 0x61) to go high */
        while ((inb(0x61) & 0x20) == 0) {
            io_wait();
        }

        /* Restore port 0x61 */
        outb(0x61, status & 0xFC);
    }
}


