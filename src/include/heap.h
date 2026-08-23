/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_HEAP_H
#define GEMIOS_HEAP_H

#include "types.h"

void heap_init(void *start_addr, size_t size);
void *kmalloc(size_t size);
void *kcalloc(size_t num, size_t size);
void *kmalloc_aligned(size_t size, size_t alignment);
void *krealloc(void *ptr, size_t new_size);
void kfree(void *ptr);

void heap_stats(size_t *total, size_t *used, size_t *free);

#endif /* GEMIOS_HEAP_H */
