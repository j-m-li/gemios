/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "gdt.h"
#include "io.h"
#include "string.h"

#define GDT_ENTRIES 5

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr gp;

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;

    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F);

    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access = access;
}

void gdt_init(void) {
    gp.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gp.base = (uint32_t)&gdt;

    /* 0x00: Null descriptor */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* 0x08: Kernel Code segment (0..4GB, Ring 0, Exec/Read) */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    /* 0x10: Kernel Data segment (0..4GB, Ring 0, Read/Write) */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    /* 0x18: User Code segment (0..4GB, Ring 3, Exec/Read) */
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    /* 0x20: User Data segment (0..4GB, Ring 3, Read/Write) */
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    load_gdt(&gp);
}
