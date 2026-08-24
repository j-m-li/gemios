# This is free and unencumbered software released into the public domain.
# GEMIOS Preemptive Real-Time Operating System

# Multiboot header and kernel entry point for 32-bit x86
.set ALIGN,    1<<0            # align loaded modules on page boundaries
.set MEMINFO,  1<<1            # provide memory map
.set VIDEOINFO,1<<2            # provide video info
.set FLAGS,    ALIGN | MEMINFO | VIDEOINFO # this is the Multiboot 'flag' field
.set MAGIC,    0x1BADB002      # 'magic number' lets bootloader find the header
.set CHECKSUM, -(MAGIC + FLAGS)# checksum of above, to prove we are multiboot

.section .multiboot, "a"
.align 4
multiboot_header:
.long MAGIC
.long FLAGS
.long CHECKSUM
       /* header_addr */
.long   multiboot_header
        /* load_addr */
.long   _start
        /* load_end_addr */
.long   _kernel_end
        /* bss_end_addr */
.long  stack_top 
        /* entry_addr */
.long   _start
.long 0
.long 640
.long 480
.long 32

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

    # Enable Fast A20 Gate
    inb $0x92, %al
    testb $0x02, %al
    jnz 1f
    orb $0x02, %al
    andb $0xFE, %al
    outb %al, $0x92
1:

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
