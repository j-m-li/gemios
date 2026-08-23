/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "mmu.h"
#include "string.h"

extern uint32_t _kernel_end;

static uint32_t *pmm_bitmap = NULL;
static size_t total_pages = 0;
static size_t used_pages = 0;
static uintptr_t pmm_start_addr = 0;

#define BITMAP_SET(bit)   (pmm_bitmap[(bit) / 32] |= (1U << ((bit) % 32)))
#define BITMAP_CLEAR(bit) (pmm_bitmap[(bit) / 32] &= ~(1U << ((bit) % 32)))
#define BITMAP_TEST(bit)  (pmm_bitmap[(bit) / 32] & (1U << ((bit) % 32)))

void pmm_init(uint32_t mem_size_kb) {
    size_t bitmap_size;
    uintptr_t kernel_end_addr;
    size_t start_page;
    size_t p;

    if (mem_size_kb == 0) {
        mem_size_kb = 128 * 1024; /* Default 128 MB if not provided */
    }

    total_pages = (mem_size_kb * 1024) / PAGE_SIZE;
    bitmap_size = DIV_ROUND_UP(total_pages, 8);

    kernel_end_addr = (uintptr_t)&_kernel_end;
    pmm_bitmap = (uint32_t*)ALIGN_UP(kernel_end_addr, 4096);

    /* Mark all pages as used initially */
    memset(pmm_bitmap, 0xFF, bitmap_size);

    pmm_start_addr = ALIGN_UP((uintptr_t)pmm_bitmap + bitmap_size, PAGE_SIZE);

    /* Free usable pages from pmm_start_addr up to total_pages */
    start_page = pmm_start_addr / PAGE_SIZE;
    used_pages = total_pages;

    for (p = start_page; p < total_pages; p++) {
        BITMAP_CLEAR(p);
        used_pages--;
    }
}

phys_addr_t pmm_alloc_page(void) {
    return pmm_alloc_pages(1);
}

phys_addr_t pmm_alloc_pages(size_t count) {
    size_t start_page;
    size_t contiguous;
    size_t found_start;
    size_t p;
    size_t i;

    if (count == 0) return 0;

    start_page = pmm_start_addr / PAGE_SIZE;
    contiguous = 0;
    found_start = 0;

    for (p = start_page; p < total_pages; p++) {
        if (!BITMAP_TEST(p)) {
            if (contiguous == 0) found_start = p;
            contiguous++;
            if (contiguous == count) {
                for (i = 0; i < count; i++) {
                    BITMAP_SET(found_start + i);
                    used_pages++;
                }
                return (phys_addr_t)(found_start * PAGE_SIZE);
            }
        } else {
            contiguous = 0;
        }
    }

    return 0; /* Out of memory */
}

phys_addr_t pmm_alloc_pages_aligned(size_t count, size_t alignment) {
    size_t align_pages;
    size_t start_page;
    size_t p;
    size_t i;

    if (count == 0) return 0;
    if (alignment < PAGE_SIZE) alignment = PAGE_SIZE;
    align_pages = alignment / PAGE_SIZE;

    start_page = ALIGN_UP(pmm_start_addr / PAGE_SIZE, align_pages);

    for (p = start_page; p + count <= total_pages; p += align_pages) {
        bool match = true;
        for (i = 0; i < count; i++) {
            if (BITMAP_TEST(p + i)) {
                match = false;
                break;
            }
        }
        if (match) {
            for (i = 0; i < count; i++) {
                BITMAP_SET(p + i);
                used_pages++;
            }
            return (phys_addr_t)(p * PAGE_SIZE);
        }
    }

    return 0;
}

void pmm_free_page(phys_addr_t addr) {
    pmm_free_pages(addr, 1);
}

void pmm_free_pages(phys_addr_t addr, size_t count) {
    size_t start_page;
    size_t i;

    if (addr == 0 || count == 0) return;

    start_page = addr / PAGE_SIZE;
    for (i = 0; i < count; i++) {
        size_t p = start_page + i;
        if (p < total_pages && BITMAP_TEST(p)) {
            BITMAP_CLEAR(p);
            used_pages--;
        }
    }
}

size_t pmm_get_total_pages(void) {
    return total_pages;
}

size_t pmm_get_free_pages(void) {
    return total_pages - used_pages;
}

size_t pmm_get_used_pages(void) {
    return used_pages;
}
