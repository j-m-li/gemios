/*
 * This is free and unencumbered software released into the public domain.
 * GEMOS Preemptive Real-Time Operating System
 */

#include "fat.h"
#include "heap.h"
#include "string.h"

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

int fat_mount(block_dev_t *bdev, fat_fs_t *fs) {
    if (!bdev || !fs) return -1;

    memset(fs, 0, sizeof(fat_fs_t));
    fs->bdev = bdev;

    uint8_t sector[512];
    if (bdev->read(bdev, 0, 1, sector) != 0) {
        return -1;
    }

    fat_bpb_t *bpb = (fat_bpb_t*)sector;
    if (bpb->bytes_per_sector == 0 || bpb->sectors_per_cluster == 0) {
        return -1;
    }

    fs->bytes_per_sector = bpb->bytes_per_sector;
    fs->sectors_per_cluster = bpb->sectors_per_cluster;
    fs->bytes_per_cluster = fs->bytes_per_sector * fs->sectors_per_cluster;
    fs->reserved_sectors = bpb->reserved_sector_count;

    uint32_t total_sectors = (bpb->total_sectors_16 != 0) ? bpb->total_sectors_16 : bpb->total_sectors_32;

    if (bpb->fat_size_16 != 0) {
        // FAT12 or FAT16
        fs->fat_size_sectors = bpb->fat_size_16;
        fs->fat_start_sector = fs->reserved_sectors;
        fs->root_dir_start_sector = fs->fat_start_sector + (bpb->num_fats * fs->fat_size_sectors);
        fs->root_dir_sectors = ((bpb->root_entry_count * 32) + (fs->bytes_per_sector - 1)) / fs->bytes_per_sector;
        fs->data_start_sector = fs->root_dir_start_sector + fs->root_dir_sectors;
    } else {
        // FAT32
        fs->fat_size_sectors = bpb->fat_size_32;
        fs->fat_start_sector = fs->reserved_sectors;
        fs->root_dir_start_sector = 0;
        fs->root_dir_sectors = 0;
        fs->data_start_sector = fs->fat_start_sector + (bpb->num_fats * fs->fat_size_sectors);
        fs->root_cluster = bpb->root_cluster;
    }

    uint32_t data_sectors = total_sectors - fs->data_start_sector;
    fs->total_clusters = data_sectors / fs->sectors_per_cluster;

    if (fs->total_clusters < 4085) {
        fs->type = FAT_TYPE_FAT12;
    } else if (fs->total_clusters < 65525) {
        fs->type = FAT_TYPE_FAT16;
    } else {
        fs->type = FAT_TYPE_FAT32;
        if (fs->root_cluster == 0) {
            fs->root_cluster = 2;
        }
    }

    return 0;
}

static void format_fat_name(const char *raw, char *out) {
    int i, j = 0;
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

static uint32_t get_next_cluster(fat_fs_t *fs, uint32_t cluster) {
    if (cluster < 2) return 0xFFFFFFFF;

    uint8_t sector[512];

    if (fs->type == FAT_TYPE_FAT16) {
        uint32_t fat_offset = cluster * 2;
        uint32_t fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
        uint32_t ent_offset = fat_offset % fs->bytes_per_sector;

        if (fs->bdev->read(fs->bdev, fat_sector, 1, sector) != 0) return 0xFFFFFFFF;
        uint16_t next = *(uint16_t*)&sector[ent_offset];
        if (next < 2 || next >= 0xFFF8) return 0xFFFFFFFF; // EOF or free/reserved
        return next;
    } else if (fs->type == FAT_TYPE_FAT32) {
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
        uint32_t ent_offset = fat_offset % fs->bytes_per_sector;

        if (fs->bdev->read(fs->bdev, fat_sector, 1, sector) != 0) return 0xFFFFFFFF;
        uint32_t next = (*(uint32_t*)&sector[ent_offset]) & 0x0FFFFFFF;
        if (next < 2 || next >= 0x0FFFFFF8) return 0xFFFFFFFF; // EOF or free/reserved
        return next;
    }

    return 0xFFFFFFFF;
}

static int read_cluster(fat_fs_t *fs, uint32_t cluster, void *buf) {
    if (cluster < 2) return -1;
    uint32_t start_sec = fs->data_start_sector + ((cluster - 2) * fs->sectors_per_cluster);
    for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
        if (fs->bdev->read(fs->bdev, start_sec + s, 1, (uint8_t*)buf + (s * fs->bytes_per_sector)) != 0) {
            return -1;
        }
    }
    return 0;
}

int fat_list_root(fat_fs_t *fs) {
    if (!fs || !fs->bdev) return -1;

    kprintf("\n--- Filesystem on %s (FAT%d) ---\n",
            fs->bdev->name,
            (fs->type == FAT_TYPE_FAT12) ? 12 : (fs->type == FAT_TYPE_FAT16 ? 16 : 32));
    kprintf("%-16s %-8s %10s\n", "Name", "Type", "Size (bytes)");
    kprintf("----------------------------------------\n");

    int files_found = 0;

    if (fs->type == FAT_TYPE_FAT32) {
        // FAT32: Root directory is a cluster chain
        uint32_t cur_cluster = fs->root_cluster;
        uint8_t *cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
        if (!cluster_buf) return -1;

        while (cur_cluster >= 2 && cur_cluster < 0x0FFFFFF8) {
            if (read_cluster(fs, cur_cluster, cluster_buf) != 0) {
                break;
            }

            size_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat_dir_entry_t);
            fat_dir_entry_t *entries = (fat_dir_entry_t*)cluster_buf;

            for (size_t e = 0; e < entries_per_cluster; e++) {
                if ((uint8_t)entries[e].name[0] == 0x00) {
                    kfree(cluster_buf);
                    goto done; // End of directory
                }
                if ((uint8_t)entries[e].name[0] == 0xE5) {
                    continue; // Deleted
                }
                if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) {
                    continue;
                }

                char filename[16];
                format_fat_name(entries[e].name, filename);

                const char *type_str = (entries[e].attr & FAT_ATTR_DIRECTORY) ? "<DIR>" : "FILE";
                kprintf("%-16s %-8s %10u\n", filename, type_str, entries[e].file_size);
                files_found++;
            }

            cur_cluster = get_next_cluster(fs, cur_cluster);
        }

        kfree(cluster_buf);
    } else {
        // FAT12 / FAT16: Fixed root directory sectors
        uint8_t sector[512];
        for (uint32_t s = 0; s < fs->root_dir_sectors; s++) {
            if (fs->bdev->read(fs->bdev, fs->root_dir_start_sector + s, 1, sector) != 0) {
                break;
            }

            fat_dir_entry_t *entries = (fat_dir_entry_t*)sector;
            for (int e = 0; e < 16; e++) {
                if ((uint8_t)entries[e].name[0] == 0x00) {
                    goto done; // End of directory
                }
                if ((uint8_t)entries[e].name[0] == 0xE5) {
                    continue; // Deleted entry
                }
                if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) {
                    continue;
                }

                char filename[16];
                format_fat_name(entries[e].name, filename);

                const char *type_str = (entries[e].attr & FAT_ATTR_DIRECTORY) ? "<DIR>" : "FILE";
                kprintf("%-16s %-8s %10u\n", filename, type_str, entries[e].file_size);
                files_found++;
            }
        }
    }

done:
    kprintf("----------------------------------------\n");
    kprintf("Total: %d item(s)\n", files_found);
    return files_found;
}

int fat_read_file(fat_fs_t *fs, const char *filename, void *buf, size_t max_len, size_t *out_len) {
    if (!fs || !fs->bdev || !filename || !buf) return -1;

    fat_dir_entry_t target_entry;
    bool found = false;

    if (fs->type == FAT_TYPE_FAT32) {
        // Search in FAT32 Root Directory Cluster Chain
        uint32_t cur_cluster = fs->root_cluster;
        uint8_t *cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
        if (!cluster_buf) return -1;

        while (cur_cluster >= 2 && cur_cluster < 0x0FFFFFF8 && !found) {
            if (read_cluster(fs, cur_cluster, cluster_buf) != 0) {
                break;
            }

            size_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat_dir_entry_t);
            fat_dir_entry_t *entries = (fat_dir_entry_t*)cluster_buf;

            for (size_t e = 0; e < entries_per_cluster; e++) {
                if ((uint8_t)entries[e].name[0] == 0x00) break;
                if ((uint8_t)entries[e].name[0] == 0xE5) continue;
                if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) continue;

                char name[16];
                format_fat_name(entries[e].name, name);

                if (fat_name_equals(name, filename)) {
                    target_entry = entries[e];
                    found = true;
                    break;
                }
            }

            cur_cluster = get_next_cluster(fs, cur_cluster);
        }

        kfree(cluster_buf);
    } else {
        // Search in FAT12/FAT16 Root Directory Sectors
        uint8_t sector[512];
        for (uint32_t s = 0; s < fs->root_dir_sectors && !found; s++) {
            if (fs->bdev->read(fs->bdev, fs->root_dir_start_sector + s, 1, sector) != 0) break;

            fat_dir_entry_t *entries = (fat_dir_entry_t*)sector;
            for (int e = 0; e < 16; e++) {
                if ((uint8_t)entries[e].name[0] == 0x00) break;
                if ((uint8_t)entries[e].name[0] == 0xE5) continue;
                if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) continue;

                char name[16];
                format_fat_name(entries[e].name, name);

                if (fat_name_equals(name, filename)) {
                    target_entry = entries[e];
                    found = true;
                    break;
                }
            }
        }
    }

    if (!found) return -1; // File not found

    uint32_t start_cluster = ((uint32_t)target_entry.fst_clus_hi << 16) | target_entry.fst_clus_lo;
    uint32_t file_size = target_entry.file_size;
    size_t to_read = MIN(file_size, max_len);

    if (to_read == 0 || start_cluster < 2) {
        if (out_len) *out_len = 0;
        return 0;
    }

    uint8_t *cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -1;

    size_t bytes_read = 0;
    uint32_t cur_cluster = start_cluster;
    uint32_t eof_limit = (fs->type == FAT_TYPE_FAT32) ? 0x0FFFFFF8 : 0xFFF8;

    while (cur_cluster >= 2 && cur_cluster < eof_limit && bytes_read < to_read) {
        if (read_cluster(fs, cur_cluster, cluster_buf) != 0) {
            break;
        }

        size_t chunk = MIN(fs->bytes_per_cluster, to_read - bytes_read);
        if (chunk == 0) break;
        memcpy((uint8_t*)buf + bytes_read, cluster_buf, chunk);
        bytes_read += chunk;

        cur_cluster = get_next_cluster(fs, cur_cluster);
    }

    kfree(cluster_buf);
    if (out_len) *out_len = bytes_read;

    return 0;
}
