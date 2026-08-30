/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_FAT_H
#define GEMIOS_FAT_H

#include "types.h"
#include "blockdev.h"

#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LFN       (FAT_ATTR_READ_ONLY | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM | FAT_ATTR_VOLUME_ID)

#define FAT_LFN_LAST_MASK  0x40
#define FAT_LFN_SEQ_MASK   0x1F
#define FAT_LFN_MAX_CHARS  255

/* 32-byte FAT Long File Name (LFN) Directory Entry */
struct fat_lfn_entry {
    uint8_t  order;          /* Sequence number: bits 0-4 are sequence (1..31), bit 6 (0x40) is LAST_LFN_ENTRY */
    uint8_t  name1[10];      /* Unicode UTF-16LE characters 1-5 (offset 1-10) */
    uint8_t  attr;           /* Attributes: MUST BE 0x0F (FAT_ATTR_LFN) */
    uint8_t  type;           /* Reserved/Type: 0x00 for LFN sub-component */
    uint8_t  checksum;       /* Checksum of the 8.3 short filename alias */
    uint8_t  name2[12];      /* Unicode UTF-16LE characters 6-11 (offset 14-25) */
    uint8_t  fst_clus_lo[2]; /* First cluster low: MUST BE 0x0000 */
    uint8_t  name3[4];       /* Unicode UTF-16LE characters 12-13 (offset 28-31) */
};
typedef struct fat_lfn_entry fat_lfn_entry_t;

/* FAT Boot Sector / BPB */
struct fat_bpb {
    uint8_t  jmp_boot[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    /* FAT32 Extended Fields (starting at offset 36) */
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
};
typedef struct fat_bpb fat_bpb_t;

/* Standard 32-byte FAT Directory Entry */
struct fat_dir_entry {
    char     name[11];
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;
    uint32_t file_size;
};
typedef struct fat_dir_entry fat_dir_entry_t;

typedef enum {
    FAT_TYPE_UNKNOWN = 0,
    FAT_TYPE_FAT12,
    FAT_TYPE_FAT16,
    FAT_TYPE_FAT32
} fat_type_t;

typedef struct fat_fs {
    block_dev_t *bdev;
    fat_type_t type;
    uint32_t lba_offset;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t reserved_sectors;
    uint32_t num_fats;
    uint32_t fat_start_sector;
    uint32_t fat_size_sectors;
    uint32_t root_dir_start_sector;
    uint32_t root_dir_sectors;
    uint32_t data_start_sector;
    uint32_t total_clusters;
    uint32_t root_cluster; /* For FAT32 */
} fat_fs_t;

int fat_mount(block_dev_t *bdev, fat_fs_t *fs);
int fat_list_root(fat_fs_t *fs);
int fat_list_dir(fat_fs_t *fs, const char *path);
int fat_is_dir(fat_fs_t *fs, const char *path);
int fat_read_file(fat_fs_t *fs, const char *path, void *buf, size_t max_len, size_t *out_len);
int fat_write_file(fat_fs_t *fs, const char *path, const void *buf, size_t len);
int fat_mkdir(fat_fs_t *fs, const char *path);
int fat_remove(fat_fs_t *fs, const char *path, bool recursive);
int fat_delete_file(fat_fs_t *fs, const char *path);
int fat_stat(fat_fs_t *fs, const char *path, uint32_t *out_size, uint8_t *out_attr);
int fat_copy_file(fat_fs_t *src_fs, const char *src_path, fat_fs_t *dst_fs, const char *dst_path);
int fat_copy_dir(fat_fs_t *src_fs, const char *src_path, fat_fs_t *dst_fs, const char *dst_path);

#endif /* GEMIOS_FAT_H */
