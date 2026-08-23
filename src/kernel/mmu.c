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

void pmm_init(uint32_t mem_size_kb, uint32_t mmap_addr, uint32_t mmap_length) {
    size_t bitmap_size;
    uintptr_t kernel_end_addr;
    uint32_t max_addr;
    uint32_t mmap_offset;

    if (mmap_addr != 0 && mmap_length > 0) {
        max_addr = 0;
        mmap_offset = 0;
        while (mmap_offset < mmap_length) {
            struct multiboot_mmap_entry *entry;
            entry = (struct multiboot_mmap_entry*)(uintptr_t)(mmap_addr + mmap_offset);
            if (entry->type == 1 && entry->addr_high == 0) {
                uint32_t end_addr = entry->addr_low + entry->len_low;
                if (end_addr > max_addr) {
                    max_addr = end_addr;
                }
            }
            mmap_offset += entry->size + sizeof(entry->size);
        }
        if (max_addr > 0) {
            total_pages = max_addr / PAGE_SIZE;
        } else {
            total_pages = (mem_size_kb * 1024) / PAGE_SIZE;
        }
    } else {
        if (mem_size_kb == 0) {
            mem_size_kb = 128 * 1024; /* Default 128 MB if not provided */
        }
        total_pages = (mem_size_kb * 1024) / PAGE_SIZE;
    }

    bitmap_size = DIV_ROUND_UP(total_pages, 8);
    kernel_end_addr = (uintptr_t)&_kernel_end;
    pmm_bitmap = (uint32_t*)ALIGN_UP(kernel_end_addr, 4096);

    /* Mark all pages as used/reserved initially */
    memset(pmm_bitmap, 0xFF, bitmap_size);
    used_pages = total_pages;

    pmm_start_addr = ALIGN_UP((uintptr_t)pmm_bitmap + bitmap_size, PAGE_SIZE);

    if (mmap_addr != 0 && mmap_length > 0) {
        mmap_offset = 0;
        while (mmap_offset < mmap_length) {
            struct multiboot_mmap_entry *entry;
            entry = (struct multiboot_mmap_entry*)(uintptr_t)(mmap_addr + mmap_offset);
            if (entry->type == 1 && entry->addr_high == 0) {
                uint32_t start;
                uint32_t end;
                size_t p;

                start = entry->addr_low;
                end = entry->addr_low + entry->len_low;

                if (start < pmm_start_addr) {
                    start = (uint32_t)pmm_start_addr;
                }

                if (start < end) {
                    size_t start_p = start / PAGE_SIZE;
                    size_t end_p = end / PAGE_SIZE;
                    if (end_p > total_pages) end_p = total_pages;

                    for (p = start_p; p < end_p; p++) {
                        BITMAP_CLEAR(p);
                        used_pages--;
                    }
                }
            }
            mmap_offset += entry->size + sizeof(entry->size);
        }
    } else {
        size_t start_page;
        size_t p;
        start_page = pmm_start_addr / PAGE_SIZE;
        for (p = start_page; p < total_pages; p++) {
            BITMAP_CLEAR(p);
            used_pages--;
        }
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
