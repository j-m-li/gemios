/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_MMU_H
#define GEMIOS_MMU_H

#include "types.h"
#include "multiboot.h"

#define PAGE_SIZE 4096

void pmm_init(uint32_t mem_size_kb, uint32_t mmap_addr, uint32_t mmap_length);
phys_addr_t pmm_alloc_page(void);
phys_addr_t pmm_alloc_pages(size_t count);
phys_addr_t pmm_alloc_pages_aligned(size_t count, size_t alignment);
void pmm_free_page(phys_addr_t addr);
void pmm_free_pages(phys_addr_t addr, size_t count);

size_t pmm_get_total_pages(void);
size_t pmm_get_free_pages(void);
size_t pmm_get_used_pages(void);

#endif /* GEMIOS_MMU_H */
