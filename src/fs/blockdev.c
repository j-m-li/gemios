/*
 * This is free and unencumbered software released into the public domain.
 * GEMOS Preemptive Real-Time Operating System
 */

#include "blockdev.h"
#include "string.h"

static block_dev_t *registered_bdevs[MAX_BLOCK_DEVS];
static size_t bdev_count = 0;

void blockdev_init(void) {
    memset(registered_bdevs, 0, sizeof(registered_bdevs));
    bdev_count = 0;
}

int blockdev_register(block_dev_t *bdev) {
    if (!bdev || bdev_count >= MAX_BLOCK_DEVS) return -1;

    registered_bdevs[bdev_count++] = bdev;
    kprintf("[BlockDev] Registered '%s' (%u blocks, %u bytes/block, %u MB)\n",
            bdev->name, bdev->total_blocks, bdev->block_size,
            (uint32_t)(((uint64_t)bdev->total_blocks * bdev->block_size) / (1024 * 1024)));
    return 0;
}

void blockdev_unregister(block_dev_t *bdev) {
    for (size_t i = 0; i < bdev_count; i++) {
        if (registered_bdevs[i] == bdev) {
            for (size_t j = i; j < bdev_count - 1; j++) {
                registered_bdevs[j] = registered_bdevs[j + 1];
            }
            bdev_count--;
            break;
        }
    }
}

block_dev_t *blockdev_get(const char *name) {
    for (size_t i = 0; i < bdev_count; i++) {
        if (strcmp(registered_bdevs[i]->name, name) == 0) {
            return registered_bdevs[i];
        }
    }
    return NULL;
}

block_dev_t *blockdev_get_by_index(size_t index) {
    if (index < bdev_count) {
        return registered_bdevs[index];
    }
    return NULL;
}

size_t blockdev_count(void) {
    return bdev_count;
}
