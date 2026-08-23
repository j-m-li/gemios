/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "heap.h"
#include "string.h"
#include "io.h"

#define HEAP_MAGIC 0xCAFEBABE

typedef struct heap_block {
    uint32_t magic;
    size_t size;
    bool is_free;
    struct heap_block *next;
    struct heap_block *prev;
} heap_block_t;

static uint8_t *heap_start = NULL;
static size_t heap_total_size = 0;
static heap_block_t *block_head = NULL;

void heap_init(void *start_addr, size_t size) {
    heap_start = (uint8_t*)ALIGN_UP((uintptr_t)start_addr, 16);
    heap_total_size = size;

    block_head = (heap_block_t*)heap_start;
    block_head->magic = HEAP_MAGIC;
    block_head->size = size - sizeof(heap_block_t);
    block_head->is_free = true;
    block_head->next = NULL;
    block_head->prev = NULL;
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    uint32_t flags = irq_save();
    size = ALIGN_UP(size, 8);
    heap_block_t *curr = block_head;

    while (curr) {
        if (curr->magic != HEAP_MAGIC) {
            kprint_color(0x4F, "[HEAP CORRUPTION DETECTED at %p]\n", curr);
            irq_restore(flags);
            return NULL;
        }

        if (curr->is_free && curr->size >= size) {
            // Check if we can split this block
            if (curr->size >= size + sizeof(heap_block_t) + 16) {
                heap_block_t *new_block = (heap_block_t*)((uint8_t*)curr + sizeof(heap_block_t) + size);
                new_block->magic = HEAP_MAGIC;
                new_block->size = curr->size - size - sizeof(heap_block_t);
                new_block->is_free = true;
                new_block->next = curr->next;
                new_block->prev = curr;

                if (curr->next) {
                    curr->next->prev = new_block;
                }
                curr->next = new_block;
                curr->size = size;
            }

            curr->is_free = false;
            irq_restore(flags);
            return (void*)((uint8_t*)curr + sizeof(heap_block_t));
        }

        curr = curr->next;
    }

    irq_restore(flags);
    return NULL; // Out of memory
}

void *kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void *ptr = kmalloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *kmalloc_aligned(size_t size, size_t alignment) {
    if (alignment <= 8) {
        return kmalloc(size);
    }

    size_t total = size + alignment + sizeof(void*);
    void *raw = kmalloc(total);
    if (!raw) return NULL;

    uintptr_t raw_addr = (uintptr_t)raw + sizeof(void*);
    uintptr_t aligned_addr = ALIGN_UP(raw_addr, alignment);

    void **ptr_slot = (void**)(aligned_addr - sizeof(void*));
    *ptr_slot = raw;

    return (void*)aligned_addr;
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    uint32_t flags = irq_save();
    heap_block_t *block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    if (block->magic != HEAP_MAGIC) {
        irq_restore(flags);
        return NULL;
    }

    if (block->size >= new_size) {
        irq_restore(flags);
        return ptr;
    }

    size_t old_size = block->size;
    irq_restore(flags);

    void *new_ptr = kmalloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, old_size);
        kfree(ptr);
    }
    return new_ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;

    uint32_t flags = irq_save();

    // Check if pointer is aligned allocation
    heap_block_t *block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    if (block->magic != HEAP_MAGIC) {
        // It might be an aligned pointer
        void **ptr_slot = (void**)((uintptr_t)ptr - sizeof(void*));
        void *raw = *ptr_slot;
        block = (heap_block_t*)((uint8_t*)raw - sizeof(heap_block_t));
        if (block->magic != HEAP_MAGIC) {
            kprint_color(0x4F, "[HEAP kfree: Invalid pointer %p]\n", ptr);
            irq_restore(flags);
            return;
        }
    }

    block->is_free = true;

    // Coalesce with next block if free
    if (block->next && block->next->is_free) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }

    // Coalesce with prev block if free
    if (block->prev && block->prev->is_free) {
        block->prev->size += sizeof(heap_block_t) + block->size;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
    }

    irq_restore(flags);
}

void heap_stats(size_t *total, size_t *used, size_t *free_mem) {
    uint32_t flags = irq_save();
    size_t u = 0, f = 0;
    heap_block_t *curr = block_head;

    while (curr) {
        if (curr->magic == HEAP_MAGIC) {
            if (curr->is_free) {
                f += curr->size;
            } else {
                u += curr->size;
            }
        }
        curr = curr->next;
    }

    if (total) *total = heap_total_size;
    if (used) *used = u;
    if (free_mem) *free_mem = f;
    irq_restore(flags);
}
