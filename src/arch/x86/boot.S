# This is free and unencumbered software released into the public domain.
# GEMOS Preemptive Real-Time Operating System

# Multiboot header and kernel entry point for 32-bit x86
.set ALIGN,    1<<0             # align loaded modules on page boundaries
.set MEMINFO,  1<<1             # provide memory map
.set FLAGS,    ALIGN | MEMINFO  # this is the Multiboot 'flag' field
.set MAGIC,    0x1BADB002       # 'magic number' lets bootloader find the header
.set CHECKSUM, -(MAGIC + FLAGS) # checksum of above, to prove we are multiboot

.section .multiboot, "a"
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .bss
.align 16
stack_bottom:
.skip 65536 # 64 KB initial kernel stack
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    # Disable interrupts
    cli

    # Setup initial stack
    mov $stack_top, %esp

    # Reset EFLAGS
    pushl $0
    popf

    # Push multiboot parameters (magic in eax, info struct in ebx)
    push %ebx
    push %eax

    # Call C entry point
    call kmain

    # Hang if kmain returns
    cli
1:  hlt
    jmp 1b

.size _start, . - _start
