/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 *
 * Low-level x86 CPU and I/O hardware assembly routines.
 */

.section .text

.global outb
.type outb, @function
outb:
    movw 4(%esp), %dx
    movb 8(%esp), %al
    outb %al, %dx
    ret

.global inb
.type inb, @function
inb:
    movw 4(%esp), %dx
    xorl %eax, %eax
    inb %dx, %al
    ret

.global outw
.type outw, @function
outw:
    movw 4(%esp), %dx
    movw 8(%esp), %ax
    outw %ax, %dx
    ret

.global inw
.type inw, @function
inw:
    movw 4(%esp), %dx
    xorl %eax, %eax
    inw %dx, %ax
    ret

.global outl
.type outl, @function
outl:
    movw 4(%esp), %dx
    movl 8(%esp), %eax
    outl %eax, %dx
    ret

.global inl
.type inl, @function
inl:
    movw 4(%esp), %dx
    inl %dx, %eax
    ret

.global io_wait
.type io_wait, @function
io_wait:
    outb %al, $0x80
    ret

.global cli
.type cli, @function
cli:
    cli
    ret

.global sti
.type sti, @function
sti:
    sti
    ret

.global hlt
.type hlt, @function
hlt:
    hlt
    ret

.global read_eflags
.type read_eflags, @function
read_eflags:
    pushfl
    popl %eax
    ret

.global load_gdt
.type load_gdt, @function
load_gdt:
    movl 4(%esp), %eax
    lgdt (%eax)
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    ljmp $0x08, $.gdt_flush_label
.gdt_flush_label:
    ret

.global load_idt
.type load_idt, @function
load_idt:
    movl 4(%esp), %eax
    lidt (%eax)
    ret

.global arch_trigger_yield
.type arch_trigger_yield, @function
arch_trigger_yield:
    int $0x80
    ret

.global rtos_start_first_task
.type rtos_start_first_task, @function
rtos_start_first_task:
    movl 4(%esp), %esp
    popl %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    popa
    addl $8, %esp
    iret

.global arch_reboot
.type arch_reboot, @function
arch_reboot:
    pushl $0
    pushl $0
    lidt (%esp)
    int $3
1:  hlt
    jmp 1b

.global arch_shutdown
.type arch_shutdown, @function
arch_shutdown:
    /* QEMU modern ACPI shutdown */
    movw $0x2000, %ax
    movw $0x0604, %dx
    outw %ax, %dx

    /* QEMU old / Bochs ACPI shutdown */
    movw $0x2000, %ax
    movw $0xB004, %dx
    outw %ax, %dx

    /* VirtualBox ACPI shutdown */
    movw $0x3400, %ax
    movw $0x4004, %dx
    outw %ax, %dx

    /* QEMU isa-debug-exit */
    movb $0x00, %al
    movw $0x0501, %dx
    outb %al, %dx

    /* Bochs shutdown string */
    movw $0x8900, %dx
    movw $0x5348, %ax
    outw %ax, %dx
    movw $0x5554, %ax
    outw %ax, %dx
    movw $0x444F, %ax
    outw %ax, %dx
    movw $0x574E, %ax
    outw %ax, %dx

    /* Fallback: disable interrupts and halt CPU */
    cli
2:  hlt
    jmp 2b

.global cpu_has_apic
.type cpu_has_apic, @function
cpu_has_apic:
    pushl %ebx
    movl $1, %eax
    cpuid
    shrl $9, %edx
    andl $1, %edx
    movl %edx, %eax
    popl %ebx
    ret

.global cpu_get_msr
.type cpu_get_msr, @function
cpu_get_msr:
    movl 4(%esp), %ecx
    rdmsr
    movl 8(%esp), %ecx
    testl %ecx, %ecx
    jz .Lget_msr_done
    movl %edx, (%ecx)
.Lget_msr_done:
    ret

.global cpu_set_msr
.type cpu_set_msr, @function
cpu_set_msr:
    movl 4(%esp), %ecx
    movl 8(%esp), %eax
    movl 12(%esp), %edx
    wrmsr
    ret

.globl memcpy
.type memcpy, @function
memcpy:
    pushl %esi
    pushl %edi

    movl 12(%esp), %edi      # EDI = dest
    movl 16(%esp), %esi      # ESI = src
    movl 20(%esp), %ecx      # ECX = n

    cld

    testl %ecx, %ecx
    jz .Lmemcpy_done

    # Copy dwords first: ECX / 4
    movl %ecx, %eax
    shrl $2, %ecx
    rep movsl

    # Copy remaining trailing bytes: EAX % 4
    movl %eax, %ecx
    andl $3, %ecx
    rep movsb

.Lmemcpy_done:
    movl 12(%esp), %eax      # Return original dest pointer
    popl %edi
    popl %esi
    ret


