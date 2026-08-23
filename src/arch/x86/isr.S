/*
 * This is free and unencumbered software released into the public domain.
 * GEMOS Preemptive Real-Time Operating System
 */

.section .text

.macro ISR_NOERRCODE num
.global isr\num
isr\num:
    pushl $0            # Dummy error code
    pushl $\num         # Interrupt number
    jmp isr_common_stub
.endm

.macro ISR_ERRCODE num
.global isr\num
isr\num:
    pushl $\num         # Interrupt number (error code already pushed by CPU)
    jmp isr_common_stub
.endm

.macro IRQ_STUB irq_num, int_num
.global irq\irq_num
irq\irq_num:
    pushl $0            # Dummy error code
    pushl $\int_num     # Interrupt number
    jmp isr_common_stub
.endm

# CPU Exceptions (0..31)
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30
ISR_NOERRCODE 31

# Hardware IRQs (32..47)
IRQ_STUB 0, 32
IRQ_STUB 1, 33
IRQ_STUB 2, 34
IRQ_STUB 3, 35
IRQ_STUB 4, 36
IRQ_STUB 5, 37
IRQ_STUB 6, 38
IRQ_STUB 7, 39
IRQ_STUB 8, 40
IRQ_STUB 9, 41
IRQ_STUB 10, 42
IRQ_STUB 11, 43
IRQ_STUB 12, 44
IRQ_STUB 13, 45
IRQ_STUB 14, 46
IRQ_STUB 15, 47

# Syscall / Preemptive Yield vector 0x80 (128)
ISR_NOERRCODE 128

.extern isr_handler

isr_common_stub:
    pusha               # Pushes edi, esi, ebp, esp, ebx, edx, ecx, eax

    mov %ds, %ax        # Save data segment descriptor
    pushl %eax

    mov $0x10, %ax      # Load kernel data segment
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    pushl %esp          # Pass pointer to registers_t struct
    call isr_handler
    movl %eax, %esp     # Switch stack to scheduled task (handles preemption)

    popl %eax           # Restore data segment
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    popa                # Restore general purpose registers
    addl $8, %esp       # Clean up int_no and err_code
    iret                # Return to interrupted / scheduled task
