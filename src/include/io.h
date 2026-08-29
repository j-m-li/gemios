/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_IO_H
#define GEMIOS_IO_H

#include "types.h"

/* Low-level hardware port I/O routines implemented in assembly */
void outb(uint16_t port, uint8_t val);
uint8_t inb(uint16_t port);
void outw(uint16_t port, uint16_t val);
uint16_t inw(uint16_t port);
void outl(uint16_t port, uint32_t val);
uint32_t inl(uint16_t port);
void io_wait(void);

/* Low-level CPU control routines implemented in assembly */
void cli(void);
void sti(void);
void hlt(void);
uint32_t read_eflags(void);
void load_gdt(const void *gdt_ptr);
void load_idt(const void *idt_ptr);
void arch_trigger_yield(void);
void rtos_start_first_task(uint32_t *stack_ptr);
void arch_reboot(void);
void arch_shutdown(void);
bool cpu_has_apic(void);
bool cpu_has_tsc(void);
uint32_t cpu_rdtsc(uint32_t *hi);
uint32_t cpu_rdtsc_lo(void);
void cpu_pause(void);
void cpu_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx);
uint32_t cpu_get_msr(uint32_t msr, uint32_t *hi);
void cpu_set_msr(uint32_t msr, uint32_t lo, uint32_t hi);

/* High-level IRQ state helpers */
uint32_t irq_save(void);
void irq_restore(uint32_t flags);
void memory_barrier(void);

/* Memory-Mapped I/O (MMIO) access functions */
uint8_t mmio_read8(uintptr_t addr);
uint16_t mmio_read16(uintptr_t addr);
uint32_t mmio_read32(uintptr_t addr);
uint32_t mmio_read64_lo(uintptr_t addr);

void mmio_write8(uintptr_t addr, uint8_t val);
void mmio_write16(uintptr_t addr, uint16_t val);
void mmio_write32(uintptr_t addr, uint32_t val);
void mmio_write64(uintptr_t addr, uint32_t low, uint32_t high);

#endif /* GEMIOS_IO_H */
