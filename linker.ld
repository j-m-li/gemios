ENTRY(_start)
OUTPUT_FORMAT("elf32-i386")

SECTIONS {
    . = 0x100000;

    .multiboot ALIGN(4) : {
        *(.multiboot)
    }

    .text ALIGN(4096) : {
        *(.text)
        *(.text.*)
    }

    .rodata ALIGN(4096) : {
        *(.rodata)
        *(.rodata.*)
    }

    .data ALIGN(4096) : {
        *(.data)
        *(.data.*)
    }

    .bss ALIGN(4096) : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    . = ALIGN(4096);
    _kernel_end = .;
}
