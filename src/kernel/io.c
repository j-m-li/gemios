/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "io.h"

uint32_t irq_save(void) {
    uint32_t flags;
    flags = read_eflags();
    cli();
    return flags;
}

void irq_restore(uint32_t flags) {
    if (flags & 0x200) {
        sti();
    } else {
        cli();
    }
}

void memory_barrier(void) {
    /* Barrier is achieved through function call boundary in C90 */
}

uint8_t mmio_read8(uintptr_t addr) {
    return *(volatile uint8_t*)addr;
}

uint16_t mmio_read16(uintptr_t addr) {
    return *(volatile uint16_t*)addr;
}

uint32_t mmio_read32(uintptr_t addr) {
    return *(volatile uint32_t*)addr;
}

uint32_t mmio_read64_lo(uintptr_t addr) {
    return *(volatile uint32_t*)addr;
}

void mmio_write8(uintptr_t addr, uint8_t val) {
    *(volatile uint8_t*)addr = val;
}

void mmio_write16(uintptr_t addr, uint16_t val) {
    *(volatile uint16_t*)addr = val;
}

void mmio_write32(uintptr_t addr, uint32_t val) {
    *(volatile uint32_t*)addr = val;
}

void mmio_write64(uintptr_t addr, uint32_t low, uint32_t high) {
    *(volatile uint32_t*)addr = low;
    *(volatile uint32_t*)(addr + 4) = high;
}
