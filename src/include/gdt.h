/*
 * This is free and unencumbered software released into the public domain.
 * GEMOS Preemptive Real-Time Operating System
 */

#ifndef GEMOS_GDT_H
#define GEMOS_GDT_H

#include "types.h"

#define KERNEL_CS 0x08
#define KERNEL_DS 0x10

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} PACKED;

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} PACKED;

void gdt_init(void);

#endif /* GEMOS_GDT_H */
