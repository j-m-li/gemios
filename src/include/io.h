/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_IO_H
#define GEMIOS_IO_H

#include "types.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static inline void cli(void) {
    __asm__ volatile ("cli" ::: "memory");
}

static inline void sti(void) {
    __asm__ volatile ("sti" ::: "memory");
}

static inline void hlt(void) {
    __asm__ volatile ("hlt");
}

static inline uint32_t read_eflags(void) {
    uint32_t eflags;
    __asm__ volatile ("pushfl; popl %0" : "=r"(eflags));
    return eflags;
}

static inline uint32_t irq_save(void) {
    uint32_t flags = read_eflags();
    cli();
    return flags;
}

static inline void irq_restore(uint32_t flags) {
    if (flags & 0x200) {
        sti();
    } else {
        cli();
    }
}

static inline void memory_barrier(void) {
    __asm__ volatile ("" ::: "memory");
}

static inline uint8_t mmio_read8(uintptr_t addr) {
    return *(volatile uint8_t*)addr;
}

static inline uint16_t mmio_read16(uintptr_t addr) {
    return *(volatile uint16_t*)addr;
}

static inline uint32_t mmio_read32(uintptr_t addr) {
    return *(volatile uint32_t*)addr;
}

static inline uint64_t mmio_read64(uintptr_t addr) {
    uint32_t low = *(volatile uint32_t*)addr;
    uint32_t high = *(volatile uint32_t*)(addr + 4);
    return ((uint64_t)high << 32) | low;
}

static inline void mmio_write8(uintptr_t addr, uint8_t val) {
    *(volatile uint8_t*)addr = val;
}

static inline void mmio_write16(uintptr_t addr, uint16_t val) {
    *(volatile uint16_t*)addr = val;
}

static inline void mmio_write32(uintptr_t addr, uint32_t val) {
    *(volatile uint32_t*)addr = val;
}

static inline void mmio_write64(uintptr_t addr, uint64_t val) {
    *(volatile uint32_t*)addr = (uint32_t)val;
    *(volatile uint32_t*)(addr + 4) = (uint32_t)(val >> 32);
}

#endif /* GEMIOS_IO_H */
