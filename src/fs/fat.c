/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "fat.h"
#include "heap.h"
#include "string.h"
#include "vga.h"

typedef struct {
    bool is_root_fixed;    /* true for FAT12/FAT16 root directory */
    uint32_t cluster;      /* cluster for FAT32 root (fs->root_cluster) or subdirectories (>=2) */
} fat_dir_t;

static char to_upper_char(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    return c;
}

static bool fat_name_equals(const char *s1, const char *s2) {
    if (!s1 || !s2) return false;
    while (*s1 && *s2) {
        if (to_upper_char(*s1) != to_upper_char(*s2)) return false;
        s1++;
        s2++;
    }
    return (*s1 == '\0' && *s2 == '\0');
}

static uint16_t fat_read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t fat_read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool is_valid_fat_bpb(const uint8_t *sector, uint32_t *out_total_sectors) {
    uint32_t total;
    uint16_t bytes_per_sec;
    uint8_t sec_per_clus;
    uint16_t rsvd_sec_cnt;
    uint8_t num_fats;
    uint16_t tot_sec_16;
    uint32_t tot_sec_32;

    /* Check boot jump instruction (0xEB or 0xE9) */
    if (sector[0] != 0xEB && sector[0] != 0xE9) {
        return false;
    }

    bytes_per_sec = fat_read_le16(&sector[11]);
    /* Bytes per sector must be standard 512, 1024, 2048, or 4096 */
    if (bytes_per_sec != 512 && bytes_per_sec != 1024 &&
        bytes_per_sec != 2048 && bytes_per_sec != 4096) {
        return false;
    }

    sec_per_clus = sector[13];
    /* Sectors per cluster must be non-zero power of 2 (1, 2, 4, 8, 16, 32, 64, 128) */
    if (sec_per_clus == 0 || (sec_per_clus & (sec_per_clus - 1)) != 0) {
        return false;
    }

    rsvd_sec_cnt = fat_read_le16(&sector[14]);
    /* Reserved sector count must be >= 1 */
    if (rsvd_sec_cnt == 0) {
        return false;
    }

    num_fats = sector[16];
    /* Num FATs typically 1 or 2 */
    if (num_fats == 0 || num_fats > 4) {
        return false;
    }

    /* Must have boot sector signature 0x55, 0xAA at offset 510 */
    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        return false;
    }

    tot_sec_16 = fat_read_le16(&sector[19]);
    tot_sec_32 = fat_read_le32(&sector[32]);
    total = (tot_sec_16 != 0) ? (uint32_t)tot_sec_16 : tot_sec_32;
    if (total == 0) {
        return false;
    }

    if (out_total_sectors) *out_total_sectors = total;
    return true;
}

int fat_mount(block_dev_t *bdev, fat_fs_t *fs) {
    uint8_t sector[512];
    uint32_t lba_offset;
    uint32_t total_sectors;
    uint32_t data_start_relative;
    uint32_t data_sectors;

    if (!bdev || !fs) return -1;

    memset(fs, 0, sizeof(fat_fs_t));
    fs->bdev = bdev;

    if (bdev->read(bdev, 0, 1, sector) != 0) {
        return -1;
    }

    lba_offset = 0;
    total_sectors = 0;

    if (is_valid_fat_bpb(sector, &total_sectors)) {
        /* Sector 0 is a direct VBR (Superfloppy / raw FAT partition) */
        lba_offset = 0;
    } else if (sector[510] == 0x55 && sector[511] == 0xAA) {
        /* Sector 0 is an MBR! Probe MBR partition table (Partitions 1-4) */
        bool found_part = false;
        int p;
        for (p = 0; p < 4; p++) {
            uint8_t *part_entry = &sector[0x1BE + (p * 16)];
            uint32_t part_start = fat_read_le32(&part_entry[8]);
            uint32_t part_size = fat_read_le32(&part_entry[12]);

            if (part_start > 0 && part_size > 0) {
                uint8_t part_sector[512];
                if (bdev->read(bdev, part_start, 1, part_sector) == 0) {
                    if (is_valid_fat_bpb(part_sector, &total_sectors)) {
                        lba_offset = part_start;
                        memcpy(sector, part_sector, 512);
                        found_part = true;
                        break;
                    }
                }
            }
        }
        if (!found_part) {
            return -1; /* No valid FAT partition found in MBR */
        }
    } else {
        return -1; /* Not a valid FAT filesystem */
    }

    fs->lba_offset = lba_offset;
    fs->bytes_per_sector = (uint32_t)fat_read_le16(&sector[11]);
    fs->sectors_per_cluster = (uint32_t)sector[13];
    fs->bytes_per_cluster = fs->bytes_per_sector * fs->sectors_per_cluster;
    fs->reserved_sectors = (uint32_t)fat_read_le16(&sector[14]);
    fs->num_fats = (uint32_t)sector[16];

    {
        uint16_t fat_sz16 = fat_read_le16(&sector[22]);
        uint16_t root_ent_cnt = fat_read_le16(&sector[17]);
        if (fat_sz16 != 0) {
            /* FAT12 or FAT16 */
            fs->fat_size_sectors = (uint32_t)fat_sz16;
            fs->fat_start_sector = fs->lba_offset + fs->reserved_sectors;
            fs->root_dir_start_sector = fs->fat_start_sector + (fs->num_fats * fs->fat_size_sectors);
            fs->root_dir_sectors = (((uint32_t)root_ent_cnt * 32) + (fs->bytes_per_sector - 1)) / fs->bytes_per_sector;
            fs->data_start_sector = fs->root_dir_start_sector + fs->root_dir_sectors;
            fs->root_cluster = 0;
        } else {
            /* FAT32 */
            fs->fat_size_sectors = fat_read_le32(&sector[36]);
            fs->fat_start_sector = fs->lba_offset + fs->reserved_sectors;
            fs->root_dir_start_sector = 0;
            fs->root_dir_sectors = 0;
            fs->data_start_sector = fs->fat_start_sector + (fs->num_fats * fs->fat_size_sectors);
            fs->root_cluster = fat_read_le32(&sector[44]);
        }
    }

    if (fs->fat_size_sectors == 0 || fs->bytes_per_cluster == 0) {
        return -1;
    }

    data_start_relative = fs->data_start_sector - fs->lba_offset;
    data_sectors = (total_sectors > data_start_relative) ? (total_sectors - data_start_relative) : 0;
    fs->total_clusters = data_sectors / fs->sectors_per_cluster;

    if (fs->total_clusters < 4085) {
        fs->type = FAT_TYPE_FAT12;
    } else if (fs->total_clusters < 65525) {
        fs->type = FAT_TYPE_FAT16;
    } else {
        fs->type = FAT_TYPE_FAT32;
        if (fs->root_cluster < 2) {
            fs->root_cluster = 2;
        }
    }

    return 0;
}

static void format_fat_name(const char *raw, char *out) {
    int i;
    int j;

    if (raw[0] == '.' && raw[1] == ' ') {
        out[0] = '.';
        out[1] = '\0';
        return;
    }
    if (raw[0] == '.' && raw[1] == '.' && raw[2] == ' ') {
        out[0] = '.';
        out[1] = '.';
        out[2] = '\0';
        return;
    }

    j = 0;
    for (i = 0; i < 8 && raw[i] != ' '; i++) {
        out[j++] = raw[i];
    }
    if (raw[8] != ' ') {
        out[j++] = '.';
        for (i = 8; i < 11 && raw[i] != ' '; i++) {
            out[j++] = raw[i];
        }
    }
    out[j] = '\0';
}

static void to_fat_83_name(const char *src, char *dest) {
    int d;

    memset(dest, ' ', 11);
    if (strcmp(src, ".") == 0) {
        dest[0] = '.';
        return;
    }
    if (strcmp(src, "..") == 0) {
        dest[0] = '.';
        dest[1] = '.';
        return;
    }

    d = 0;
    while (*src && *src != '.' && *src != '/' && *src != '\\' && d < 8) {
        dest[d++] = to_upper_char(*src++);
    }
    while (*src && *src != '.' && *src != '/' && *src != '\\') src++;
    if (*src == '.') src++;
    d = 8;
    while (*src && *src != '/' && *src != '\\' && d < 11) {
        dest[d++] = to_upper_char(*src++);
    }
}

static uint32_t get_next_cluster(fat_fs_t *fs, uint32_t cluster) {
    uint8_t sector[512];

    if (cluster < 2) return 0xFFFFFFFF;

    if (fs->type == FAT_TYPE_FAT16) {
        uint32_t fat_offset;
        uint32_t fat_sector;
        uint32_t ent_offset;
        uint16_t next;

        fat_offset = cluster * 2;
        fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
        ent_offset = fat_offset % fs->bytes_per_sector;

        if (fs->bdev->read(fs->bdev, fat_sector, 1, sector) != 0) return 0xFFFFFFFF;
        next = *(uint16_t*)&sector[ent_offset];
        if (next < 2 || next >= 0xFFF8 || next == cluster) return 0xFFFFFFFF; /* EOF, reserved, or cycle */
        return next;
    } else if (fs->type == FAT_TYPE_FAT32) {
        uint32_t fat_offset;
        uint32_t fat_sector;
        uint32_t ent_offset;
        uint32_t next;

        fat_offset = cluster * 4;
        fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
        ent_offset = fat_offset % fs->bytes_per_sector;

        if (fs->bdev->read(fs->bdev, fat_sector, 1, sector) != 0) return 0xFFFFFFFF;
        next = (*(uint32_t*)&sector[ent_offset]) & 0x0FFFFFFF;
        if (next < 2 || next >= 0x0FFFFFF8 || next == cluster) return 0xFFFFFFFF; /* EOF, reserved, or cycle */
        return next;
    }

    return 0xFFFFFFFF;
}

static int set_fat_entry(fat_fs_t *fs, uint32_t cluster, uint32_t value) {
    uint8_t sector[512];

    if (cluster < 2) return -1;

    if (fs->type == FAT_TYPE_FAT16) {
        uint32_t fat_offset;
        uint32_t fat_sector;
        uint32_t ent_offset;

        fat_offset = cluster * 2;
        fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
        ent_offset = fat_offset % fs->bytes_per_sector;

        if (fs->bdev->read(fs->bdev, fat_sector, 1, sector) != 0) return -1;
        *(uint16_t*)&sector[ent_offset] = (uint16_t)value;
        if (fs->bdev->write(fs->bdev, fat_sector, 1, sector) != 0) return -1;
        if (fs->num_fats > 1 && fs->fat_size_sectors > 0) {
            fs->bdev->write(fs->bdev, fat_sector + fs->fat_size_sectors, 1, sector);
        }
        return 0;
    } else if (fs->type == FAT_TYPE_FAT32) {
        uint32_t fat_offset;
        uint32_t fat_sector;
        uint32_t ent_offset;
        uint32_t orig;

        fat_offset = cluster * 4;
        fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
        ent_offset = fat_offset % fs->bytes_per_sector;

        if (fs->bdev->read(fs->bdev, fat_sector, 1, sector) != 0) return -1;
        orig = *(uint32_t*)&sector[ent_offset];
        *(uint32_t*)&sector[ent_offset] = (orig & 0xF0000000) | (value & 0x0FFFFFFF);
        if (fs->bdev->write(fs->bdev, fat_sector, 1, sector) != 0) return -1;
        if (fs->num_fats > 1 && fs->fat_size_sectors > 0) {
            fs->bdev->write(fs->bdev, fat_sector + fs->fat_size_sectors, 1, sector);
        }
        return 0;
    }

    return -1;
}

static uint32_t alloc_free_cluster(fat_fs_t *fs) {
    uint32_t eof_val;
    uint8_t sector[512];
    uint32_t cur_sector;
    uint32_t max_scan;
    uint32_t c;

    eof_val = (fs->type == FAT_TYPE_FAT32) ? 0x0FFFFFFF : 0xFFFF;
    cur_sector = 0xFFFFFFFF;
    max_scan = (fs->total_clusters > 0 && fs->total_clusters < 32768) ? fs->total_clusters : 32768;

    for (c = 2; c < max_scan + 2; c++) {
        if (fs->type == FAT_TYPE_FAT16) {
            uint32_t fat_offset;
            uint32_t fat_sector;
            uint32_t ent_offset;
            uint16_t val;

            fat_offset = c * 2;
            fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
            ent_offset = fat_offset % fs->bytes_per_sector;

            if (fat_sector != cur_sector) {
                if (fs->bdev->read(fs->bdev, fat_sector, 1, sector) != 0) return 0;
                cur_sector = fat_sector;
            }

            val = *(uint16_t*)&sector[ent_offset];
            if (val == 0) {
                set_fat_entry(fs, c, eof_val);
                return c;
            }
        } else if (fs->type == FAT_TYPE_FAT32) {
            uint32_t fat_offset;
            uint32_t fat_sector;
            uint32_t ent_offset;
            uint32_t val;

            fat_offset = c * 4;
            fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
            ent_offset = fat_offset % fs->bytes_per_sector;

            if (fat_sector != cur_sector) {
                if (fs->bdev->read(fs->bdev, fat_sector, 1, sector) != 0) return 0;
                cur_sector = fat_sector;
            }

            val = (*(uint32_t*)&sector[ent_offset]) & 0x0FFFFFFF;
            if (val == 0) {
                set_fat_entry(fs, c, eof_val);
                return c;
            }
        }
    }
    return 0;
}

static int read_cluster(fat_fs_t *fs, uint32_t cluster, void *buf) {
    uint32_t start_sec;
    if (cluster < 2 || !fs->bdev || fs->sectors_per_cluster == 0) return -1;
    start_sec = fs->data_start_sector + ((cluster - 2) * fs->sectors_per_cluster);
    return fs->bdev->read(fs->bdev, start_sec, fs->sectors_per_cluster, buf);
}

static int write_cluster(fat_fs_t *fs, uint32_t cluster, const void *buf) {
    uint32_t start_sec;
    if (cluster < 2 || !fs->bdev || fs->sectors_per_cluster == 0) return -1;
    start_sec = fs->data_start_sector + ((cluster - 2) * fs->sectors_per_cluster);
    return fs->bdev->write(fs->bdev, start_sec, fs->sectors_per_cluster, buf);
}

static fat_dir_t fat_root_dir(fat_fs_t *fs) {
    fat_dir_t d;
    if (fs->type == FAT_TYPE_FAT32) {
        d.is_root_fixed = false;
        d.cluster = fs->root_cluster;
    } else {
        d.is_root_fixed = true;
        d.cluster = 0;
    }
    return d;
}

/* Find an entry by name in directory */
static int fat_dir_find_entry(fat_fs_t *fs, fat_dir_t dir, const char *name,
                              fat_dir_entry_t *out_entry, uint32_t *out_sector, int *out_slot) {
    if (dir.is_root_fixed) {
        uint8_t sector[512];
        uint32_t max_root_sectors;
        uint32_t s;

        max_root_sectors = (fs->root_dir_sectors < 1024) ? fs->root_dir_sectors : 1024;
        for (s = 0; s < max_root_sectors; s++) {
            uint32_t cur_sec;
            fat_dir_entry_t *entries;
            int e;

            cur_sec = fs->root_dir_start_sector + s;
            if (fs->bdev->read(fs->bdev, cur_sec, 1, sector) != 0) break;

            entries = (fat_dir_entry_t*)sector;
            for (e = 0; e < 16; e++) {
                char fname[16];

                if ((uint8_t)entries[e].name[0] == 0x00) return -1; /* End of directory */
                if ((uint8_t)entries[e].name[0] == 0xE5) continue;  /* Deleted entry */
                if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) continue;

                format_fat_name(entries[e].name, fname);
                if (fat_name_equals(fname, name)) {
                    if (out_entry) *out_entry = entries[e];
                    if (out_sector) *out_sector = cur_sec;
                    if (out_slot) *out_slot = e;
                    return 0;
                }
            }
        }
        return -1;
    } else {
        uint32_t cur_cluster;
        uint8_t *cluster_buf;
        uint32_t visited;
        uint32_t eof_limit;
        size_t entries_per_cluster;

        cur_cluster = dir.cluster;
        if (cur_cluster < 2 || fs->bytes_per_cluster == 0) return -1;

        cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
        if (!cluster_buf) return -1;

        visited = 0;
        eof_limit = (fs->type == FAT_TYPE_FAT32) ? 0x0FFFFFF8 : 0xFFF8;
        entries_per_cluster = fs->bytes_per_cluster / sizeof(fat_dir_entry_t);

        while (cur_cluster >= 2 && cur_cluster < eof_limit && visited++ < 65536) {
            fat_dir_entry_t *entries;
            size_t e;
            uint32_t next;

            if (read_cluster(fs, cur_cluster, cluster_buf) != 0) break;

            entries = (fat_dir_entry_t*)cluster_buf;
            for (e = 0; e < entries_per_cluster; e++) {
                char fname[16];

                if ((uint8_t)entries[e].name[0] == 0x00) {
                    kfree(cluster_buf);
                    return -1; /* End of directory */
                }
                if ((uint8_t)entries[e].name[0] == 0xE5) continue;
                if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) continue;

                format_fat_name(entries[e].name, fname);
                if (fat_name_equals(fname, name)) {
                    uint32_t sec_offset = (e * sizeof(fat_dir_entry_t)) / fs->bytes_per_sector;
                    uint32_t cur_sec = fs->data_start_sector + ((cur_cluster - 2) * fs->sectors_per_cluster) + sec_offset;
                    int slot = e % (fs->bytes_per_sector / sizeof(fat_dir_entry_t));

                    if (out_entry) *out_entry = entries[e];
                    if (out_sector) *out_sector = cur_sec;
                    if (out_slot) *out_slot = slot;
                    kfree(cluster_buf);
                    return 0;
                }
            }

            next = get_next_cluster(fs, cur_cluster);
            if (next == cur_cluster || next < 2) break;
            cur_cluster = next;
        }

        kfree(cluster_buf);
        return -1;
    }
}

/* Find matching entry or allocate free slot in directory */
static int fat_dir_find_or_alloc_slot(fat_fs_t *fs, fat_dir_t dir, const char *name,
                                      uint32_t *out_sector, int *out_slot,
                                      fat_dir_entry_t *out_entry, bool *out_exists) {
    uint32_t free_sector;
    int free_slot;
    uint32_t last_cluster;

    *out_exists = false;
    free_sector = 0;
    free_slot = -1;
    last_cluster = 0;

    if (dir.is_root_fixed) {
        uint8_t sector[512];
        uint32_t max_root_sectors;
        uint32_t s;

        max_root_sectors = (fs->root_dir_sectors < 1024) ? fs->root_dir_sectors : 1024;
        for (s = 0; s < max_root_sectors; s++) {
            uint32_t cur_sec;
            fat_dir_entry_t *entries;
            int e;

            cur_sec = fs->root_dir_start_sector + s;
            if (fs->bdev->read(fs->bdev, cur_sec, 1, sector) != 0) break;

            entries = (fat_dir_entry_t*)sector;
            for (e = 0; e < 16; e++) {
                char fname[16];

                if ((uint8_t)entries[e].name[0] == 0x00 || (uint8_t)entries[e].name[0] == 0xE5) {
                    if (free_slot == -1) {
                        free_sector = cur_sec;
                        free_slot = e;
                    }
                    if ((uint8_t)entries[e].name[0] == 0x00) goto fixed_search_done;
                    continue;
                }
                if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) continue;

                format_fat_name(entries[e].name, fname);
                if (fat_name_equals(fname, name)) {
                    *out_exists = true;
                    *out_sector = cur_sec;
                    *out_slot = e;
                    if (out_entry) *out_entry = entries[e];
                    return 0;
                }
            }
        }

    fixed_search_done:
        if (free_slot != -1) {
            *out_sector = free_sector;
            *out_slot = free_slot;
            return 0;
        }
        return -1; /* Fixed root directory full */
    } else {
        uint32_t cur_cluster;
        uint8_t *cluster_buf;
        uint32_t visited;
        uint32_t eof_limit;
        size_t entries_per_cluster;
        uint32_t new_c;
        uint8_t *zero_buf;

        cur_cluster = dir.cluster;
        if (cur_cluster < 2 || fs->bytes_per_cluster == 0) return -1;

        cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
        if (!cluster_buf) return -1;

        visited = 0;
        eof_limit = (fs->type == FAT_TYPE_FAT32) ? 0x0FFFFFF8 : 0xFFF8;
        entries_per_cluster = fs->bytes_per_cluster / sizeof(fat_dir_entry_t);

        while (cur_cluster >= 2 && cur_cluster < eof_limit && visited++ < 65536) {
            fat_dir_entry_t *entries;
            size_t e;
            uint32_t next;

            last_cluster = cur_cluster;
            if (read_cluster(fs, cur_cluster, cluster_buf) != 0) break;

            entries = (fat_dir_entry_t*)cluster_buf;
            for (e = 0; e < entries_per_cluster; e++) {
                char fname[16];

                if ((uint8_t)entries[e].name[0] == 0x00 || (uint8_t)entries[e].name[0] == 0xE5) {
                    if (free_slot == -1) {
                        uint32_t sec_offset = (e * sizeof(fat_dir_entry_t)) / fs->bytes_per_sector;
                        free_sector = fs->data_start_sector + ((cur_cluster - 2) * fs->sectors_per_cluster) + sec_offset;
                        free_slot = e % (fs->bytes_per_sector / sizeof(fat_dir_entry_t));
                    }
                    if ((uint8_t)entries[e].name[0] == 0x00) {
                        kfree(cluster_buf);
                        goto clus_search_done;
                    }
                    continue;
                }
                if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) continue;

                format_fat_name(entries[e].name, fname);
                if (fat_name_equals(fname, name)) {
                    uint32_t sec_offset = (e * sizeof(fat_dir_entry_t)) / fs->bytes_per_sector;
                    *out_sector = fs->data_start_sector + ((cur_cluster - 2) * fs->sectors_per_cluster) + sec_offset;
                    *out_slot = e % (fs->bytes_per_sector / sizeof(fat_dir_entry_t));
                    *out_exists = true;
                    if (out_entry) *out_entry = entries[e];
                    kfree(cluster_buf);
                    return 0;
                }
            }

            next = get_next_cluster(fs, cur_cluster);
            if (next == cur_cluster || next < 2) break;
            cur_cluster = next;
        }
        kfree(cluster_buf);

    clus_search_done:
        if (free_slot != -1) {
            *out_sector = free_sector;
            *out_slot = free_slot;
            return 0;
        }

        /* Expand directory: allocate new cluster */
        new_c = alloc_free_cluster(fs);
        if (new_c == 0) return -1; /* Disk full */

        set_fat_entry(fs, last_cluster, new_c);

        /* Zero out the newly allocated directory cluster */
        zero_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
        if (zero_buf) {
            memset(zero_buf, 0, fs->bytes_per_cluster);
            write_cluster(fs, new_c, zero_buf);
            kfree(zero_buf);
        }

        *out_sector = fs->data_start_sector + ((new_c - 2) * fs->sectors_per_cluster);
        *out_slot = 0;
        return 0;
    }
}

/* Resolve a path string to a fat_dir_t directory */
static int fat_resolve_dir(fat_fs_t *fs, const char *path, fat_dir_t *out_dir) {
    fat_dir_t cur_dir;
    const char *p;
    char comp[64];

    if (!fs || !out_dir) return -1;

    cur_dir = fat_root_dir(fs);

    if (!path || path[0] == '\0') {
        *out_dir = cur_dir;
        return 0;
    }

    p = path;
    while (*p == '/' || *p == '\\') p++;
    if (*p == '\0') {
        *out_dir = cur_dir;
        return 0;
    }

    while (*p) {
        int len;
        fat_dir_entry_t ent;
        uint32_t sec;
        int slot;
        uint32_t clus;

        len = 0;
        while (*p && *p != '/' && *p != '\\' && len < 63) {
            comp[len++] = *p++;
        }
        comp[len] = '\0';
        while (*p == '/' || *p == '\\') p++;

        if (len == 0 || strcmp(comp, ".") == 0) {
            continue;
        }

        if (strcmp(comp, "..") == 0) {
            if (cur_dir.is_root_fixed || (fs->type == FAT_TYPE_FAT32 && cur_dir.cluster == fs->root_cluster)) {
                cur_dir = fat_root_dir(fs);
            } else {
                fat_dir_entry_t pent;
                uint32_t psec;
                int pslot;
                if (fat_dir_find_entry(fs, cur_dir, "..", &pent, &psec, &pslot) == 0) {
                    uint32_t pclus = ((uint32_t)pent.fst_clus_hi << 16) | pent.fst_clus_lo;
                    if (pclus == 0 || (fs->type == FAT_TYPE_FAT32 && pclus == fs->root_cluster)) {
                        cur_dir = fat_root_dir(fs);
                    } else {
                        cur_dir.is_root_fixed = false;
                        cur_dir.cluster = pclus;
                    }
                } else {
                    cur_dir = fat_root_dir(fs);
                }
            }
            continue;
        }

        if (fat_dir_find_entry(fs, cur_dir, comp, &ent, &sec, &slot) != 0) {
            return -1; /* Directory component not found */
        }
        if (!(ent.attr & FAT_ATTR_DIRECTORY)) {
            return -1; /* Not a directory */
        }

        clus = ((uint32_t)ent.fst_clus_hi << 16) | ent.fst_clus_lo;
        if (clus < 2) {
            cur_dir = fat_root_dir(fs);
        } else {
            cur_dir.is_root_fixed = false;
            cur_dir.cluster = clus;
        }
    }

    *out_dir = cur_dir;
    return 0;
}

/* Resolve a path to parent directory fat_dir_t and target base name */
static int fat_resolve_parent_and_name(fat_fs_t *fs, const char *path, fat_dir_t *out_parent_dir, char *out_name) {
    char clean_path[256];
    size_t pl;
    int last_slash;
    int i;

    if (!fs || !path || path[0] == '\0') return -1;

    pl = strlen(path);
    if (pl >= sizeof(clean_path)) pl = sizeof(clean_path) - 1;
    memcpy(clean_path, path, pl);
    clean_path[pl] = '\0';

    /* Strip trailing slashes */
    while (pl > 1 && (clean_path[pl - 1] == '/' || clean_path[pl - 1] == '\\')) {
        clean_path[--pl] = '\0';
    }

    last_slash = -1;
    for (i = (int)pl - 1; i >= 0; i--) {
        if (clean_path[i] == '/' || clean_path[i] == '\\') {
            last_slash = i;
            break;
        }
    }

    if (last_slash == -1) {
        *out_parent_dir = fat_root_dir(fs);
        strncpy(out_name, clean_path, 63);
        out_name[63] = '\0';
        return 0;
    } else if (last_slash == 0) {
        *out_parent_dir = fat_root_dir(fs);
        strncpy(out_name, clean_path + 1, 63);
        out_name[63] = '\0';
        return 0;
    } else {
        clean_path[last_slash] = '\0';
        strncpy(out_name, clean_path + last_slash + 1, 63);
        out_name[63] = '\0';
        return fat_resolve_dir(fs, clean_path, out_parent_dir);
    }
}

int fat_list_dir(fat_fs_t *fs, const char *path) {
    fat_dir_t dir;
    int files_found;

    if (!fs || !fs->bdev) return -1;

    if (path == NULL || path[0] == '\0' || strcmp(path, "/") == 0) {
        dir = fat_root_dir(fs);
        kprintf("\n--- Filesystem on %s (FAT%d) ---\n",
                fs->bdev->name,
                (fs->type == FAT_TYPE_FAT12) ? 12 : (fs->type == FAT_TYPE_FAT16 ? 16 : 32));
    } else {
        if (fat_resolve_dir(fs, path, &dir) != 0) {
            return -1;
        }
        if (dir.is_root_fixed || (fs->type == FAT_TYPE_FAT32 && dir.cluster == fs->root_cluster)) {
            kprintf("\n--- Filesystem on %s (FAT%d) ---\n",
                    fs->bdev->name,
                    (fs->type == FAT_TYPE_FAT12) ? 12 : (fs->type == FAT_TYPE_FAT16 ? 16 : 32));
        } else {
            kprintf("\n--- Directory '%s' on %s (FAT%d) ---\n",
                    path, fs->bdev->name,
                    (fs->type == FAT_TYPE_FAT12) ? 12 : (fs->type == FAT_TYPE_FAT16 ? 16 : 32));
        }
    }

    kprintf("%-16s %-8s %10s\n", "Name", "Type", "Size (bytes)");
    kprintf("----------------------------------------\n");

    files_found = 0;

    if (dir.is_root_fixed) {
        uint8_t sector[512];
        uint32_t max_root_sectors;
        uint32_t s;

        max_root_sectors = (fs->root_dir_sectors < 1024) ? fs->root_dir_sectors : 1024;
        for (s = 0; s < max_root_sectors; s++) {
            fat_dir_entry_t *entries;
            int e;

            if (fs->bdev->read(fs->bdev, fs->root_dir_start_sector + s, 1, sector) != 0) {
                break;
            }

            entries = (fat_dir_entry_t*)sector;
            for (e = 0; e < 16; e++) {
                char filename[16];
                const char *type_str;

                if ((uint8_t)entries[e].name[0] == 0x00) {
                    goto done;
                }
                if ((uint8_t)entries[e].name[0] == 0xE5) {
                    continue;
                }
                if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) {
                    continue;
                }

                format_fat_name(entries[e].name, filename);
                type_str = (entries[e].attr & FAT_ATTR_DIRECTORY) ? "<DIR>" : "FILE";
                kprintf("%-16s %-8s %10u\n", filename, type_str, entries[e].file_size);
                files_found++;
            }
        }
    } else {
        uint32_t cur_cluster;
        uint8_t *cluster_buf;
        uint32_t visited;
        uint32_t eof_limit;
        size_t entries_per_cluster;

        cur_cluster = dir.cluster;
        if (cur_cluster < 2 || fs->bytes_per_cluster == 0) return -1;

        cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
        if (!cluster_buf) return -1;

        visited = 0;
        eof_limit = (fs->type == FAT_TYPE_FAT32) ? 0x0FFFFFF8 : 0xFFF8;
        entries_per_cluster = fs->bytes_per_cluster / sizeof(fat_dir_entry_t);

        while (cur_cluster >= 2 && cur_cluster < eof_limit && visited++ < 65536) {
            fat_dir_entry_t *entries;
            size_t e;
            uint32_t next;

            if (read_cluster(fs, cur_cluster, cluster_buf) != 0) {
                break;
            }

            entries = (fat_dir_entry_t*)cluster_buf;
            for (e = 0; e < entries_per_cluster; e++) {
                char filename[16];
                const char *type_str;

                if ((uint8_t)entries[e].name[0] == 0x00) {
                    kfree(cluster_buf);
                    goto done;
                }
                if ((uint8_t)entries[e].name[0] == 0xE5) {
                    continue;
                }
                if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) {
                    continue;
                }

                format_fat_name(entries[e].name, filename);
                type_str = (entries[e].attr & FAT_ATTR_DIRECTORY) ? "<DIR>" : "FILE";
                kprintf("%-16s %-8s %10u\n", filename, type_str, entries[e].file_size);
                files_found++;
            }

            next = get_next_cluster(fs, cur_cluster);
            if (next == cur_cluster || next < 2) break;
            cur_cluster = next;
        }

        kfree(cluster_buf);
    }

done:
    kprintf("----------------------------------------\n");
    kprintf("Total: %d item(s)\n", files_found);
    return files_found;
}

int fat_list_root(fat_fs_t *fs) {
    return fat_list_dir(fs, "/");
}

int fat_is_dir(fat_fs_t *fs, const char *path) {
    fat_dir_t dir;
    if (!fs || !path) return -1;
    if (fat_resolve_dir(fs, path, &dir) == 0) {
        return 1;
    }
    return 0;
}

int fat_read_file(fat_fs_t *fs, const char *path, void *buf, size_t max_len, size_t *out_len) {
    fat_dir_t parent_dir;
    char filename[64];
    fat_dir_entry_t target_entry;
    uint32_t sector;
    int slot;
    uint32_t start_cluster;
    uint32_t file_size;
    size_t to_read;
    uint8_t *cluster_buf;
    size_t bytes_read;
    uint32_t cur_cluster;
    uint32_t eof_limit;
    uint32_t visited;

    if (!fs || !fs->bdev || !path || !buf) return -1;

    if (fat_resolve_parent_and_name(fs, path, &parent_dir, filename) != 0) {
        return -1;
    }

    if (fat_dir_find_entry(fs, parent_dir, filename, &target_entry, &sector, &slot) != 0) {
        return -1; /* File not found */
    }

    if (target_entry.attr & FAT_ATTR_DIRECTORY) {
        return -1; /* Path is a directory, not a file */
    }

    start_cluster = ((uint32_t)target_entry.fst_clus_hi << 16) | target_entry.fst_clus_lo;
    file_size = target_entry.file_size;
    to_read = MIN(file_size, max_len);

    if (to_read == 0 || start_cluster < 2) {
        if (out_len) *out_len = 0;
        return 0;
    }

    if (fs->bytes_per_cluster == 0) return -1;
    cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -1;

    bytes_read = 0;
    cur_cluster = start_cluster;
    eof_limit = (fs->type == FAT_TYPE_FAT32) ? 0x0FFFFFF8 : 0xFFF8;
    visited = 0;

    while (cur_cluster >= 2 && cur_cluster < eof_limit && bytes_read < to_read && visited++ < 65536) {
        size_t chunk;
        uint32_t next;

        if (read_cluster(fs, cur_cluster, cluster_buf) != 0) {
            break;
        }

        chunk = MIN(fs->bytes_per_cluster, to_read - bytes_read);
        if (chunk == 0) break;
        memcpy((uint8_t*)buf + bytes_read, cluster_buf, chunk);
        bytes_read += chunk;

        if (bytes_read >= to_read) break;

        next = get_next_cluster(fs, cur_cluster);
        if (next == cur_cluster || next < 2) break;
        cur_cluster = next;
    }

    kfree(cluster_buf);
    if (out_len) *out_len = bytes_read;

    return 0;
}

int fat_write_file(fat_fs_t *fs, const char *path, const void *buf, size_t len) {
    fat_dir_t parent_dir;
    char filename[64];
    uint32_t dir_sector;
    int dir_slot;
    fat_dir_entry_t entry;
    bool exists;
    int res;
    uint32_t first_cluster;
    uint8_t *cluster_buf;
    size_t bytes_written;
    uint32_t cur_cluster;
    uint32_t eof_val;
    uint8_t sector[512];
    fat_dir_entry_t *target_entry;

    if (!fs || !fs->bdev || !path) return -1;

    dir_sector = 0;
    dir_slot = -1;
    exists = false;

    if (fat_resolve_parent_and_name(fs, path, &parent_dir, filename) != 0) {
        return -1;
    }

    res = fat_dir_find_or_alloc_slot(fs, parent_dir, filename, &dir_sector, &dir_slot, &entry, &exists);
    if (res != 0) return res;

    if (exists && (entry.attr & FAT_ATTR_DIRECTORY)) {
        return -1; /* Cannot overwrite directory with a file */
    }

    first_cluster = 0;
    if (exists) {
        first_cluster = ((uint32_t)entry.fst_clus_hi << 16) | entry.fst_clus_lo;
    }

    if (first_cluster < 2) {
        first_cluster = alloc_free_cluster(fs);
        if (first_cluster == 0) return -1; /* Disk full */
    }

    /* Write data to cluster chain */
    cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -1;

    bytes_written = 0;
    cur_cluster = first_cluster;
    eof_val = (fs->type == FAT_TYPE_FAT32) ? 0x0FFFFFFF : 0xFFFF;

    while (bytes_written < len || bytes_written == 0) {
        size_t chunk;
        uint32_t next;

        memset(cluster_buf, 0, fs->bytes_per_cluster);
        chunk = MIN(fs->bytes_per_cluster, len - bytes_written);
        if (chunk > 0 && buf) {
            memcpy(cluster_buf, (const uint8_t*)buf + bytes_written, chunk);
        }
        bytes_written += (chunk > 0) ? chunk : fs->bytes_per_cluster;

        if (write_cluster(fs, cur_cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }

        if (bytes_written >= len) {
            set_fat_entry(fs, cur_cluster, eof_val);
            break;
        }

        next = get_next_cluster(fs, cur_cluster);
        if (next < 2 || (fs->type == FAT_TYPE_FAT32 ? next >= 0x0FFFFFF8 : next >= 0xFFF8)) {
            next = alloc_free_cluster(fs);
            if (next == 0) {
                kfree(cluster_buf);
                return -1; /* Disk full */
            }
            set_fat_entry(fs, cur_cluster, next);
        }
        cur_cluster = next;
    }

    kfree(cluster_buf);

    /* Update Directory Entry */
    if (fs->bdev->read(fs->bdev, dir_sector, 1, sector) != 0) return -1;
    target_entry = &((fat_dir_entry_t*)sector)[dir_slot];
    to_fat_83_name(filename, target_entry->name);
    target_entry->attr = FAT_ATTR_ARCHIVE;
    target_entry->fst_clus_lo = (uint16_t)(first_cluster & 0xFFFF);
    target_entry->fst_clus_hi = (uint16_t)((first_cluster >> 16) & 0xFFFF);
    target_entry->file_size = (uint32_t)len;

    return fs->bdev->write(fs->bdev, dir_sector, 1, sector);
}

int fat_mkdir(fat_fs_t *fs, const char *path) {
    fat_dir_t parent_dir;
    char dirname[64];
    uint32_t dir_sector;
    int dir_slot;
    fat_dir_entry_t entry;
    bool exists;
    int res;
    uint32_t new_cluster;
    uint8_t *cluster_buf;
    fat_dir_entry_t *dot_entries;
    uint32_t parent_cluster;
    uint8_t sector[512];
    fat_dir_entry_t *target_entry;

    if (!fs || !fs->bdev || !path || path[0] == '\0') return -1;

    dir_sector = 0;
    dir_slot = -1;
    exists = false;

    if (fat_resolve_parent_and_name(fs, path, &parent_dir, dirname) != 0) {
        return -1;
    }

    if (dirname[0] == '\0' || strcmp(dirname, ".") == 0 || strcmp(dirname, "..") == 0) {
        return -1;
    }

    res = fat_dir_find_or_alloc_slot(fs, parent_dir, dirname, &dir_sector, &dir_slot, &entry, &exists);
    if (res != 0) return res;
    if (exists) return -2; /* Directory or file already exists */

    /* 2. Allocate a cluster for the new directory */
    new_cluster = alloc_free_cluster(fs);
    if (new_cluster == 0) return -1; /* Disk full */

    /* 3. Initialize directory contents: '.' and '..' */
    cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -1;
    memset(cluster_buf, 0, fs->bytes_per_cluster);

    dot_entries = (fat_dir_entry_t*)cluster_buf;

    /* '.' entry (self) */
    memset(dot_entries[0].name, ' ', 11);
    dot_entries[0].name[0] = '.';
    dot_entries[0].attr = FAT_ATTR_DIRECTORY;
    dot_entries[0].fst_clus_lo = (uint16_t)(new_cluster & 0xFFFF);
    dot_entries[0].fst_clus_hi = (uint16_t)((new_cluster >> 16) & 0xFFFF);
    dot_entries[0].file_size = 0;

    /* '..' entry (parent) */
    memset(dot_entries[1].name, ' ', 11);
    dot_entries[1].name[0] = '.';
    dot_entries[1].name[1] = '.';
    dot_entries[1].attr = FAT_ATTR_DIRECTORY;

    parent_cluster = parent_dir.is_root_fixed ? 0 : parent_dir.cluster;
    if (fs->type == FAT_TYPE_FAT32 && parent_cluster == fs->root_cluster) {
        parent_cluster = 0; /* Standard FAT32 root dotdot is cluster 0 */
    }
    dot_entries[1].fst_clus_lo = (uint16_t)(parent_cluster & 0xFFFF);
    dot_entries[1].fst_clus_hi = (uint16_t)((parent_cluster >> 16) & 0xFFFF);
    dot_entries[1].file_size = 0;

    if (write_cluster(fs, new_cluster, cluster_buf) != 0) {
        kfree(cluster_buf);
        return -1;
    }
    kfree(cluster_buf);

    /* 4. Update directory entry in parent directory */
    if (fs->bdev->read(fs->bdev, dir_sector, 1, sector) != 0) return -1;
    target_entry = &((fat_dir_entry_t*)sector)[dir_slot];
    to_fat_83_name(dirname, target_entry->name);
    target_entry->attr = FAT_ATTR_DIRECTORY;
    target_entry->fst_clus_lo = (uint16_t)(new_cluster & 0xFFFF);
    target_entry->fst_clus_hi = (uint16_t)((new_cluster >> 16) & 0xFFFF);
    target_entry->file_size = 0;

    return fs->bdev->write(fs->bdev, dir_sector, 1, sector);
}

static void fat_free_cluster_chain(fat_fs_t *fs, uint32_t start_cluster) {
    uint32_t cur;
    uint32_t eof_limit;
    uint32_t visited;

    if (!fs || start_cluster < 2) return;
    cur = start_cluster;
    eof_limit = (fs->type == FAT_TYPE_FAT32) ? 0x0FFFFFF8 : 0xFFF8;
    visited = 0;

    while (cur >= 2 && cur < eof_limit && visited++ < 65536) {
        uint32_t next = get_next_cluster(fs, cur);
        set_fat_entry(fs, cur, 0);
        if (next == cur || next < 2) break;
        cur = next;
    }
}

static int fat_wipe_dir_contents(fat_fs_t *fs, uint32_t dir_cluster) {
    uint8_t *cluster_buf;
    uint32_t cur_cluster;
    uint32_t eof_limit;
    size_t entries_per_cluster;
    uint32_t visited;

    if (dir_cluster < 2 || fs->bytes_per_cluster == 0) return -1;
    cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -1;

    cur_cluster = dir_cluster;
    eof_limit = (fs->type == FAT_TYPE_FAT32) ? 0x0FFFFFF8 : 0xFFF8;
    entries_per_cluster = fs->bytes_per_cluster / sizeof(fat_dir_entry_t);
    visited = 0;

    while (cur_cluster >= 2 && cur_cluster < eof_limit && visited++ < 65536) {
        fat_dir_entry_t *entries;
        bool modified;
        size_t e;
        uint32_t next;

        if (read_cluster(fs, cur_cluster, cluster_buf) != 0) break;
        entries = (fat_dir_entry_t*)cluster_buf;
        modified = false;

        for (e = 0; e < entries_per_cluster; e++) {
            uint32_t sub_cluster;

            if ((uint8_t)entries[e].name[0] == 0x00) break;
            if ((uint8_t)entries[e].name[0] == 0xE5) continue;
            if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) continue;

            /* Skip '.' and '..' */
            if (entries[e].name[0] == '.' && (entries[e].name[1] == ' ' || (entries[e].name[1] == '.' && entries[e].name[2] == ' '))) {
                continue;
            }

            sub_cluster = ((uint32_t)entries[e].fst_clus_hi << 16) | entries[e].fst_clus_lo;
            if (entries[e].attr & FAT_ATTR_DIRECTORY) {
                if (sub_cluster >= 2) {
                    fat_wipe_dir_contents(fs, sub_cluster);
                    fat_free_cluster_chain(fs, sub_cluster);
                }
            } else {
                if (sub_cluster >= 2) {
                    fat_free_cluster_chain(fs, sub_cluster);
                }
            }
            entries[e].name[0] = (char)0xE5;
            modified = true;
        }

        if (modified) {
            write_cluster(fs, cur_cluster, cluster_buf);
        }

        next = get_next_cluster(fs, cur_cluster);
        if (next == cur_cluster || next < 2) break;
        cur_cluster = next;
    }

    kfree(cluster_buf);
    return 0;
}

static int fat_delete_entry(fat_fs_t *fs, uint32_t sector, int slot, fat_dir_entry_t *entry, bool recursive) {
    uint8_t sec_buf[512];
    fat_dir_entry_t *entries;

    if (!fs || !entry) return -1;

    if (entry->attr & FAT_ATTR_DIRECTORY) {
        uint32_t dir_cluster;
        if (!recursive) {
            return -2; /* Cannot remove directory without -r */
        }
        dir_cluster = ((uint32_t)entry->fst_clus_hi << 16) | entry->fst_clus_lo;
        if (dir_cluster >= 2) {
            fat_wipe_dir_contents(fs, dir_cluster);
            fat_free_cluster_chain(fs, dir_cluster);
        }
    } else {
        uint32_t file_cluster;
        file_cluster = ((uint32_t)entry->fst_clus_hi << 16) | entry->fst_clus_lo;
        if (file_cluster >= 2) {
            fat_free_cluster_chain(fs, file_cluster);
        }
    }

    if (fs->bdev->read(fs->bdev, sector, 1, sec_buf) != 0) return -1;
    entries = (fat_dir_entry_t*)sec_buf;
    entries[slot].name[0] = (char)0xE5;
    return fs->bdev->write(fs->bdev, sector, 1, sec_buf);
}

static bool fat_pattern_match(const char *pattern, const char *str) {
    if (!pattern || !str) return false;
    while (*pattern) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == '\0') return true;
            while (*str) {
                if (fat_pattern_match(pattern, str)) return true;
                str++;
            }
            return false;
        } else if (*pattern == '?') {
            if (*str == '\0') return false;
            pattern++;
            str++;
        } else {
            if (to_upper_char(*pattern) != to_upper_char(*str)) return false;
            pattern++;
            str++;
        }
    }
    return *str == '\0';
}

int fat_remove(fat_fs_t *fs, const char *path, bool recursive) {
    fat_dir_t parent_dir;
    char target_pattern[64];
    bool is_wildcard;
    int removed_count;

    if (!fs || !fs->bdev || !path || path[0] == '\0') return -1;

    if (fat_resolve_parent_and_name(fs, path, &parent_dir, target_pattern) != 0) {
        return -1;
    }

    if (target_pattern[0] == '\0' || strcmp(target_pattern, ".") == 0 || strcmp(target_pattern, "..") == 0) {
        return -1;
    }

    is_wildcard = (strchr(target_pattern, '*') != NULL || strchr(target_pattern, '?') != NULL);
    removed_count = 0;

    if (parent_dir.is_root_fixed) {
        uint8_t sector[512];
        uint32_t max_root_sectors;
        uint32_t s;

        max_root_sectors = (fs->root_dir_sectors < 1024) ? fs->root_dir_sectors : 1024;
        for (s = 0; s < max_root_sectors; s++) {
            uint32_t cur_sec;
            fat_dir_entry_t *entries;
            int e;

            cur_sec = fs->root_dir_start_sector + s;
            if (fs->bdev->read(fs->bdev, cur_sec, 1, sector) != 0) break;

            entries = (fat_dir_entry_t*)sector;
            for (e = 0; e < 16; e++) {
                char fname[16];
                bool match;

                if ((uint8_t)entries[e].name[0] == 0x00) goto done_search;
                if ((uint8_t)entries[e].name[0] == 0xE5) continue;
                if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) continue;

                /* Skip '.' and '..' */
                if (entries[e].name[0] == '.' && (entries[e].name[1] == ' ' || (entries[e].name[1] == '.' && entries[e].name[2] == ' '))) {
                    continue;
                }

                format_fat_name(entries[e].name, fname);

                match = false;
                if (is_wildcard) {
                    match = fat_pattern_match(target_pattern, fname);
                } else {
                    match = fat_name_equals(fname, target_pattern);
                }

                if (match) {
                    int del_res = fat_delete_entry(fs, cur_sec, e, &entries[e], recursive);
                    if (del_res == 0) {
                        removed_count++;
                        if (!is_wildcard) return 1;
                    } else if (del_res == -2 && !is_wildcard) {
                        return -2; /* Is a directory without -r */
                    }
                }
            }
        }
    } else {
        uint32_t cur_cluster;
        uint8_t *cluster_buf;
        uint32_t visited;
        uint32_t eof_limit;
        size_t entries_per_cluster;

        cur_cluster = parent_dir.cluster;
        if (cur_cluster < 2 || fs->bytes_per_cluster == 0) return -1;

        cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
        if (!cluster_buf) return -1;

        visited = 0;
        eof_limit = (fs->type == FAT_TYPE_FAT32) ? 0x0FFFFFF8 : 0xFFF8;
        entries_per_cluster = fs->bytes_per_cluster / sizeof(fat_dir_entry_t);

        while (cur_cluster >= 2 && cur_cluster < eof_limit && visited++ < 65536) {
            fat_dir_entry_t *entries;
            size_t e;
            uint32_t next;

            if (read_cluster(fs, cur_cluster, cluster_buf) != 0) break;

            entries = (fat_dir_entry_t*)cluster_buf;
            for (e = 0; e < entries_per_cluster; e++) {
                char fname[16];
                bool match;

                if ((uint8_t)entries[e].name[0] == 0x00) {
                    kfree(cluster_buf);
                    goto done_search;
                }
                if ((uint8_t)entries[e].name[0] == 0xE5) continue;
                if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) continue;

                /* Skip '.' and '..' */
                if (entries[e].name[0] == '.' && (entries[e].name[1] == ' ' || (entries[e].name[1] == '.' && entries[e].name[2] == ' '))) {
                    continue;
                }

                format_fat_name(entries[e].name, fname);

                match = false;
                if (is_wildcard) {
                    match = fat_pattern_match(target_pattern, fname);
                } else {
                    match = fat_name_equals(fname, target_pattern);
                }

                if (match) {
                    uint32_t sec_offset = (e * sizeof(fat_dir_entry_t)) / fs->bytes_per_sector;
                    uint32_t cur_sec = fs->data_start_sector + ((cur_cluster - 2) * fs->sectors_per_cluster) + sec_offset;
                    int slot = e % (fs->bytes_per_sector / sizeof(fat_dir_entry_t));

                    int del_res = fat_delete_entry(fs, cur_sec, slot, &entries[e], recursive);
                    if (del_res == 0) {
                        removed_count++;
                        if (!is_wildcard) {
                            kfree(cluster_buf);
                            return 1;
                        }
                    } else if (del_res == -2 && !is_wildcard) {
                        kfree(cluster_buf);
                        return -2; /* Is a directory without -r */
                    }
                }
            }

            next = get_next_cluster(fs, cur_cluster);
            if (next == cur_cluster || next < 2) break;
            cur_cluster = next;
        }

        kfree(cluster_buf);
    }

done_search:
    return (removed_count > 0) ? removed_count : -1;
}

int fat_delete_file(fat_fs_t *fs, const char *path) {
    return fat_remove(fs, path, false);
}
