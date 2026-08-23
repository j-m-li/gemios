/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "idt.h"
#include "gdt.h"
#include "pic.h"
#include "string.h"
#include "io.h"
#include "sched.h"

#define IDT_ENTRIES 256

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtp;
static isr_handler_t interrupt_handlers[IDT_ENTRIES];

// Extern ISR functions from isr.S
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

extern void isr128(void);

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = (base & 0xFFFF);
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

void register_interrupt_handler(uint8_t n, isr_handler_t handler) {
    interrupt_handlers[n] = handler;
}

static const char *exception_messages[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
    "Coprocessor Fault", "Alignment Check", "Machine Check", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Security Exception", "Reserved"
};

uint32_t isr_handler(registers_t *regs) {
    if (regs->int_no < 32) {
        // CPU Exception
        if (interrupt_handlers[regs->int_no]) {
            interrupt_handlers[regs->int_no](regs);
        } else {
            kprint_color(0x4F, "\n[FATAL CPU EXCEPTION %u: %s] err=0x%x eip=0x%x\n",
                         regs->int_no,
                         (regs->int_no < 32) ? exception_messages[regs->int_no] : "Unknown",
                         regs->err_code, regs->eip);
            cli();
            for (;;) { hlt(); }
        }
    } else if (regs->int_no == 32) {
        // PIT Timer IRQ0 - Preemptive Scheduler Tick
        pic_send_eoi(0);
        if (interrupt_handlers[32]) {
            interrupt_handlers[32](regs);
        }
        if (rtos_is_running()) {
            return (uint32_t)rtos_schedule_from_isr(regs);
        }
    } else if (regs->int_no >= 33 && regs->int_no < 48) {
        // Hardware IRQ
        uint8_t irq = regs->int_no - 32;
        if (interrupt_handlers[regs->int_no]) {
            interrupt_handlers[regs->int_no](regs);
        }
        pic_send_eoi(irq);
    } else if (regs->int_no == 128) {
        // Software Syscall / Yield (int 0x80)
        if (rtos_is_running()) {
            return (uint32_t)rtos_schedule_from_isr(regs);
        }
    } else {
        if (interrupt_handlers[regs->int_no]) {
            interrupt_handlers[regs->int_no](regs);
        }
    }

    return (uint32_t)regs;
}

void idt_init(void) {
    idtp.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    idtp.base = (uint32_t)&idt;

    memset(&idt, 0, sizeof(struct idt_entry) * IDT_ENTRIES);
    memset(&interrupt_handlers, 0, sizeof(isr_handler_t) * IDT_ENTRIES);

    // CPU Exceptions
    idt_set_gate(0, (uint32_t)isr0, KERNEL_CS, 0x8E);
    idt_set_gate(1, (uint32_t)isr1, KERNEL_CS, 0x8E);
    idt_set_gate(2, (uint32_t)isr2, KERNEL_CS, 0x8E);
    idt_set_gate(3, (uint32_t)isr3, KERNEL_CS, 0x8E);
    idt_set_gate(4, (uint32_t)isr4, KERNEL_CS, 0x8E);
    idt_set_gate(5, (uint32_t)isr5, KERNEL_CS, 0x8E);
    idt_set_gate(6, (uint32_t)isr6, KERNEL_CS, 0x8E);
    idt_set_gate(7, (uint32_t)isr7, KERNEL_CS, 0x8E);
    idt_set_gate(8, (uint32_t)isr8, KERNEL_CS, 0x8E);
    idt_set_gate(9, (uint32_t)isr9, KERNEL_CS, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, KERNEL_CS, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, KERNEL_CS, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, KERNEL_CS, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, KERNEL_CS, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, KERNEL_CS, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, KERNEL_CS, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, KERNEL_CS, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, KERNEL_CS, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, KERNEL_CS, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, KERNEL_CS, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, KERNEL_CS, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, KERNEL_CS, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, KERNEL_CS, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, KERNEL_CS, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, KERNEL_CS, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, KERNEL_CS, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, KERNEL_CS, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, KERNEL_CS, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, KERNEL_CS, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, KERNEL_CS, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, KERNEL_CS, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, KERNEL_CS, 0x8E);

    // Hardware IRQs
    idt_set_gate(32, (uint32_t)irq0, KERNEL_CS, 0x8E);
    idt_set_gate(33, (uint32_t)irq1, KERNEL_CS, 0x8E);
    idt_set_gate(34, (uint32_t)irq2, KERNEL_CS, 0x8E);
    idt_set_gate(35, (uint32_t)irq3, KERNEL_CS, 0x8E);
    idt_set_gate(36, (uint32_t)irq4, KERNEL_CS, 0x8E);
    idt_set_gate(37, (uint32_t)irq5, KERNEL_CS, 0x8E);
    idt_set_gate(38, (uint32_t)irq6, KERNEL_CS, 0x8E);
    idt_set_gate(39, (uint32_t)irq7, KERNEL_CS, 0x8E);
    idt_set_gate(40, (uint32_t)irq8, KERNEL_CS, 0x8E);
    idt_set_gate(41, (uint32_t)irq9, KERNEL_CS, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, KERNEL_CS, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, KERNEL_CS, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, KERNEL_CS, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, KERNEL_CS, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, KERNEL_CS, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, KERNEL_CS, 0x8E);

    // Preemptive Syscall / Yield 0x80
    idt_set_gate(128, (uint32_t)isr128, KERNEL_CS, 0xEE);

    __asm__ volatile ("lidt %0" : : "m"(idtp));
}
