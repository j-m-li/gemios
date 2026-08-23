/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_BLOCKDEV_H
#define GEMIOS_BLOCKDEV_H

#include "types.h"

struct block_dev;

typedef int (*block_read_fn)(struct block_dev *bdev, uint32_t lba, uint32_t count, void *buf);
typedef int (*block_write_fn)(struct block_dev *bdev, uint32_t lba, uint32_t count, const void *buf);

typedef struct block_dev {
    char name[16];
    uint32_t block_size;
    uint32_t total_blocks;
    block_read_fn read;
    block_write_fn write;
    void *priv;
} block_dev_t;

#define MAX_BLOCK_DEVS 8

void blockdev_init(void);
int blockdev_register(block_dev_t *bdev);
void blockdev_unregister(block_dev_t *bdev);
block_dev_t *blockdev_get(const char *name);
block_dev_t *blockdev_get_by_index(size_t index);
size_t blockdev_count(void);

#endif /* GEMIOS_BLOCKDEV_H */
