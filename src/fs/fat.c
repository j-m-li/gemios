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

static bool is_valid_fat_bpb(const uint8_t *sector, uint32_t *out_total_sectors) {
    fat_bpb_t *bpb = (fat_bpb_t*)sector;

    // Check boot jump instruction (0xEB or 0xE9)
    if (sector[0] != 0xEB && sector[0] != 0xE9) {
        return false;
    }

    // Bytes per sector must be standard 512, 1024, 2048, or 4096
    if (bpb->bytes_per_sector != 512 && bpb->bytes_per_sector != 1024 &&
        bpb->bytes_per_sector != 2048 && bpb->bytes_per_sector != 4096) {
        return false;
    }

    // Sectors per cluster must be non-zero power of 2 (1, 2, 4, 8, 16, 32, 64, 128)
    if (bpb->sectors_per_cluster == 0 || (bpb->sectors_per_cluster & (bpb->sectors_per_cluster - 1)) != 0) {
        return false;
    }

    // Reserved sector count must be >= 1
    if (bpb->reserved_sector_count == 0) {
        return false;
    }

    // Num FATs typically 1 or 2
    if (bpb->num_fats == 0 || bpb->num_fats > 4) {
        return false;
    }

    // Must have boot sector signature 0x55, 0xAA at offset 510
    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        return false;
    }

    uint32_t total = (bpb->total_sectors_16 != 0) ? bpb->total_sectors_16 : bpb->total_sectors_32;
    if (total == 0) {
        return false;
    }

    if (out_total_sectors) *out_total_sectors = total;
    return true;
}

int fat_mount(block_dev_t *bdev, fat_fs_t *fs) {
    if (!bdev || !fs) return -1;

    memset(fs, 0, sizeof(fat_fs_t));
    fs->bdev = bdev;

    uint8_t sector[512];
    if (bdev->read(bdev, 0, 1, sector) != 0) {
        return -1;
    }

    uint32_t lba_offset = 0;
    uint32_t total_sectors = 0;

    if (is_valid_fat_bpb(sector, &total_sectors)) {
        // Sector 0 is a direct VBR (Superfloppy / raw FAT partition)
        lba_offset = 0;
    } else if (sector[510] == 0x55 && sector[511] == 0xAA) {
        // Sector 0 is an MBR! Probe MBR partition table (Partitions 1-4)
        bool found_part = false;
        for (int p = 0; p < 4; p++) {
            uint8_t *part_entry = &sector[0x1BE + (p * 16)];
            uint32_t part_start = *(uint32_t*)&part_entry[8];
            uint32_t part_size = *(uint32_t*)&part_entry[12];

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
            return -1; // No valid FAT partition found in MBR
        }
    } else {
        return -1; // Not a valid FAT filesystem
    }

    fat_bpb_t *bpb = (fat_bpb_t*)sector;
    fs->lba_offset = lba_offset;
    fs->bytes_per_sector = bpb->bytes_per_sector;
    fs->sectors_per_cluster = bpb->sectors_per_cluster;
    fs->bytes_per_cluster = fs->bytes_per_sector * fs->sectors_per_cluster;
    fs->reserved_sectors = bpb->reserved_sector_count;
    fs->num_fats = bpb->num_fats;

    if (bpb->fat_size_16 != 0) {
        // FAT12 or FAT16
        fs->fat_size_sectors = bpb->fat_size_16;
        fs->fat_start_sector = fs->lba_offset + fs->reserved_sectors;
        fs->root_dir_start_sector = fs->fat_start_sector + (fs->num_fats * fs->fat_size_sectors);
        fs->root_dir_sectors = ((bpb->root_entry_count * 32) + (fs->bytes_per_sector - 1)) / fs->bytes_per_sector;
        fs->data_start_sector = fs->root_dir_start_sector + fs->root_dir_sectors;
        fs->root_cluster = 0;
    } else {
        // FAT32
        fs->fat_size_sectors = bpb->fat_size_32;
        fs->fat_start_sector = fs->lba_offset + fs->reserved_sectors;
        fs->root_dir_start_sector = 0;
        fs->root_dir_sectors = 0;
        fs->data_start_sector = fs->fat_start_sector + (fs->num_fats * fs->fat_size_sectors);
        fs->root_cluster = bpb->root_cluster;
    }

    if (fs->fat_size_sectors == 0 || fs->bytes_per_cluster == 0) {
        return -1;
    }

    uint32_t data_start_relative = fs->data_start_sector - fs->lba_offset;
    uint32_t data_sectors = (total_sectors > data_start_relative) ? (total_sectors - data_start_relative) : 0;
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

static void to_fat_83_name(const char *src, char *dest) {
    memset(dest, ' ', 11);
    int d = 0;
    while (*src && *src != '.' && d < 8) {
        dest[d++] = to_upper_char(*src++);
    }
    while (*src && *src != '.') src++;
    if (*src == '.') src++;
    d = 8;
    while (*src && d < 11) {
        dest[d++] = to_upper_char(*src++);
    }
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
        if (next < 2 || next >= 0xFFF8 || next == cluster) return 0xFFFFFFFF; // EOF, reserved, or cycle
        return next;
    } else if (fs->type == FAT_TYPE_FAT32) {
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
        uint32_t ent_offset = fat_offset % fs->bytes_per_sector;

        if (fs->bdev->read(fs->bdev, fat_sector, 1, sector) != 0) return 0xFFFFFFFF;
        uint32_t next = (*(uint32_t*)&sector[ent_offset]) & 0x0FFFFFFF;
        if (next < 2 || next >= 0x0FFFFFF8 || next == cluster) return 0xFFFFFFFF; // EOF, reserved, or cycle
        return next;
    }

    return 0xFFFFFFFF;
}

static int set_fat_entry(fat_fs_t *fs, uint32_t cluster, uint32_t value) {
    if (cluster < 2) return -1;
    uint8_t sector[512];

    if (fs->type == FAT_TYPE_FAT16) {
        uint32_t fat_offset = cluster * 2;
        uint32_t fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
        uint32_t ent_offset = fat_offset % fs->bytes_per_sector;

        if (fs->bdev->read(fs->bdev, fat_sector, 1, sector) != 0) return -1;
        *(uint16_t*)&sector[ent_offset] = (uint16_t)value;
        if (fs->bdev->write(fs->bdev, fat_sector, 1, sector) != 0) return -1;
        if (fs->num_fats > 1 && fs->fat_size_sectors > 0) {
            fs->bdev->write(fs->bdev, fat_sector + fs->fat_size_sectors, 1, sector);
        }
        return 0;
    } else if (fs->type == FAT_TYPE_FAT32) {
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
        uint32_t ent_offset = fat_offset % fs->bytes_per_sector;

        if (fs->bdev->read(fs->bdev, fat_sector, 1, sector) != 0) return -1;
        uint32_t orig = *(uint32_t*)&sector[ent_offset];
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
    uint32_t eof_val = (fs->type == FAT_TYPE_FAT32) ? 0x0FFFFFFF : 0xFFFF;
    uint8_t sector[512];
    uint32_t cur_sector = 0xFFFFFFFF;
    uint32_t max_scan = (fs->total_clusters > 0 && fs->total_clusters < 32768) ? fs->total_clusters : 32768;

    for (uint32_t c = 2; c < max_scan + 2; c++) {
        if (fs->type == FAT_TYPE_FAT16) {
            uint32_t fat_offset = c * 2;
            uint32_t fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
            uint32_t ent_offset = fat_offset % fs->bytes_per_sector;

            if (fat_sector != cur_sector) {
                if (fs->bdev->read(fs->bdev, fat_sector, 1, sector) != 0) return 0;
                cur_sector = fat_sector;
            }

            uint16_t val = *(uint16_t*)&sector[ent_offset];
            if (val == 0) {
                set_fat_entry(fs, c, eof_val);
                return c;
            }
        } else if (fs->type == FAT_TYPE_FAT32) {
            uint32_t fat_offset = c * 4;
            uint32_t fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
            uint32_t ent_offset = fat_offset % fs->bytes_per_sector;

            if (fat_sector != cur_sector) {
                if (fs->bdev->read(fs->bdev, fat_sector, 1, sector) != 0) return 0;
                cur_sector = fat_sector;
            }

            uint32_t val = (*(uint32_t*)&sector[ent_offset]) & 0x0FFFFFFF;
            if (val == 0) {
                set_fat_entry(fs, c, eof_val);
                return c;
            }
        }
    }
    return 0;
}

static int read_cluster(fat_fs_t *fs, uint32_t cluster, void *buf) {
    if (cluster < 2 || !fs->bdev || fs->sectors_per_cluster == 0) return -1;
    uint32_t start_sec = fs->data_start_sector + ((cluster - 2) * fs->sectors_per_cluster);
    return fs->bdev->read(fs->bdev, start_sec, fs->sectors_per_cluster, buf);
}

static int write_cluster(fat_fs_t *fs, uint32_t cluster, const void *buf) {
    if (cluster < 2 || !fs->bdev || fs->sectors_per_cluster == 0) return -1;
    uint32_t start_sec = fs->data_start_sector + ((cluster - 2) * fs->sectors_per_cluster);
    return fs->bdev->write(fs->bdev, start_sec, fs->sectors_per_cluster, buf);
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
        if (cur_cluster < 2 || fs->bytes_per_cluster == 0) return -1;

        uint8_t *cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
        if (!cluster_buf) return -1;

        uint32_t visited = 0;
        uint32_t max_clusters = (fs->total_clusters > 0 && fs->total_clusters < 65536) ? (fs->total_clusters + 10) : 65536;

        while (cur_cluster >= 2 && cur_cluster < 0x0FFFFFF8 && visited++ < max_clusters) {
            if (read_cluster(fs, cur_cluster, cluster_buf) != 0) {
                break;
            }

            size_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat_dir_entry_t);
            if (entries_per_cluster == 0) break;
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

            uint32_t next = get_next_cluster(fs, cur_cluster);
            if (next == cur_cluster || next < 2) break;
            cur_cluster = next;
        }

        kfree(cluster_buf);
    } else {
        // FAT12 / FAT16: Fixed root directory sectors
        uint8_t sector[512];
        uint32_t max_root_sectors = (fs->root_dir_sectors < 1024) ? fs->root_dir_sectors : 1024;
        for (uint32_t s = 0; s < max_root_sectors; s++) {
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
        if (cur_cluster < 2 || fs->bytes_per_cluster == 0) return -1;
        uint8_t *cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
        if (!cluster_buf) return -1;

        uint32_t visited = 0;
        uint32_t max_clusters = (fs->total_clusters > 0 && fs->total_clusters < 65536) ? (fs->total_clusters + 10) : 65536;

        while (cur_cluster >= 2 && cur_cluster < 0x0FFFFFF8 && !found && visited++ < max_clusters) {
            if (read_cluster(fs, cur_cluster, cluster_buf) != 0) {
                break;
            }

            size_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat_dir_entry_t);
            if (entries_per_cluster == 0) break;
            fat_dir_entry_t *entries = (fat_dir_entry_t*)cluster_buf;
            bool eod = false;

            for (size_t e = 0; e < entries_per_cluster; e++) {
                if ((uint8_t)entries[e].name[0] == 0x00) { eod = true; break; }
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

            if (eod) break;
            uint32_t next = get_next_cluster(fs, cur_cluster);
            if (next == cur_cluster || next < 2) break;
            cur_cluster = next;
        }

        kfree(cluster_buf);
    } else {
        // Search in FAT12/FAT16 Root Directory Sectors
        uint8_t sector[512];
        bool eod = false;
        uint32_t max_root_sectors = (fs->root_dir_sectors < 1024) ? fs->root_dir_sectors : 1024;
        for (uint32_t s = 0; s < max_root_sectors && !found && !eod; s++) {
            if (fs->bdev->read(fs->bdev, fs->root_dir_start_sector + s, 1, sector) != 0) break;

            fat_dir_entry_t *entries = (fat_dir_entry_t*)sector;
            for (int e = 0; e < 16; e++) {
                if ((uint8_t)entries[e].name[0] == 0x00) { eod = true; break; }
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

    if (fs->bytes_per_cluster == 0) return -1;
    uint8_t *cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -1;

    size_t bytes_read = 0;
    uint32_t cur_cluster = start_cluster;
    uint32_t eof_limit = (fs->type == FAT_TYPE_FAT32) ? 0x0FFFFFF8 : 0xFFF8;
    uint32_t visited = 0;

    while (cur_cluster >= 2 && cur_cluster < eof_limit && bytes_read < to_read && visited++ < 65536) {
        if (read_cluster(fs, cur_cluster, cluster_buf) != 0) {
            break;
        }

        size_t chunk = MIN(fs->bytes_per_cluster, to_read - bytes_read);
        if (chunk == 0) break;
        memcpy((uint8_t*)buf + bytes_read, cluster_buf, chunk);
        bytes_read += chunk;

        if (bytes_read >= to_read) break;

        uint32_t next = get_next_cluster(fs, cur_cluster);
        if (next == cur_cluster || next < 2) break;
        cur_cluster = next;
    }

    kfree(cluster_buf);
    if (out_len) *out_len = bytes_read;

    return 0;
}

int fat_write_file(fat_fs_t *fs, const char *filename, const void *buf, size_t len) {
    if (!fs || !fs->bdev || !filename) return -1;

    char fat83_name[11];
    to_fat_83_name(filename, fat83_name);

    uint8_t sector[512];
    uint32_t dir_sector = 0;
    int dir_slot = -1;
    fat_dir_entry_t *target_entry = NULL;
    bool exists = false;

    // Search for existing file or free slot in Root Directory
    if (fs->type == FAT_TYPE_FAT16 || fs->type == FAT_TYPE_FAT12) {
        uint32_t max_root_sectors = (fs->root_dir_sectors < 1024) ? fs->root_dir_sectors : 1024;
        for (uint32_t s = 0; s < max_root_sectors; s++) {
            if (fs->bdev->read(fs->bdev, fs->root_dir_start_sector + s, 1, sector) != 0) break;

            fat_dir_entry_t *entries = (fat_dir_entry_t*)sector;
            for (int e = 0; e < 16; e++) {
                if ((uint8_t)entries[e].name[0] == 0x00 || (uint8_t)entries[e].name[0] == 0xE5) {
                    if (dir_slot == -1) {
                        dir_sector = fs->root_dir_start_sector + s;
                        dir_slot = e;
                    }
                    if ((uint8_t)entries[e].name[0] == 0x00) goto search_done;
                    continue;
                }
                if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) continue;

                char name[16];
                format_fat_name(entries[e].name, name);
                if (fat_name_equals(name, filename)) {
                    dir_sector = fs->root_dir_start_sector + s;
                    dir_slot = e;
                    exists = true;
                    goto search_done;
                }
            }
        }
    } else if (fs->type == FAT_TYPE_FAT32) {
        uint32_t cur_clus = fs->root_cluster;
        if (cur_clus < 2 || fs->bytes_per_cluster == 0) return -1;
        uint8_t *cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
        if (!cluster_buf) return -1;

        uint32_t visited = 0;
        uint32_t max_clusters = (fs->total_clusters > 0 && fs->total_clusters < 65536) ? (fs->total_clusters + 10) : 65536;

        while (cur_clus >= 2 && cur_clus < 0x0FFFFFF8 && visited++ < max_clusters) {
            if (read_cluster(fs, cur_clus, cluster_buf) != 0) break;

            size_t entries_per_clus = fs->bytes_per_cluster / sizeof(fat_dir_entry_t);
            if (entries_per_clus == 0) break;
            fat_dir_entry_t *entries = (fat_dir_entry_t*)cluster_buf;

            for (size_t e = 0; e < entries_per_clus; e++) {
                if ((uint8_t)entries[e].name[0] == 0x00 || (uint8_t)entries[e].name[0] == 0xE5) {
                    if (dir_slot == -1) {
                        uint32_t sec_offset = (e * sizeof(fat_dir_entry_t)) / fs->bytes_per_sector;
                        dir_sector = fs->data_start_sector + ((cur_clus - 2) * fs->sectors_per_cluster) + sec_offset;
                        dir_slot = e % (fs->bytes_per_sector / sizeof(fat_dir_entry_t));
                    }
                    if ((uint8_t)entries[e].name[0] == 0x00) {
                        kfree(cluster_buf);
                        goto search_done;
                    }
                    continue;
                }
                if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) continue;

                char name[16];
                format_fat_name(entries[e].name, name);
                if (fat_name_equals(name, filename)) {
                    uint32_t sec_offset = (e * sizeof(fat_dir_entry_t)) / fs->bytes_per_sector;
                    dir_sector = fs->data_start_sector + ((cur_clus - 2) * fs->sectors_per_cluster) + sec_offset;
                    dir_slot = e % (fs->bytes_per_sector / sizeof(fat_dir_entry_t));
                    exists = true;
                    kfree(cluster_buf);
                    goto search_done;
                }
            }
            uint32_t next = get_next_cluster(fs, cur_clus);
            if (next == cur_clus || next < 2) break;
            cur_clus = next;
        }
        kfree(cluster_buf);
    }

search_done:
    if (dir_slot == -1) return -1; // Directory is full

    if (fs->bdev->read(fs->bdev, dir_sector, 1, sector) != 0) return -1;
    target_entry = &((fat_dir_entry_t*)sector)[dir_slot];

    uint32_t first_cluster = 0;
    if (exists) {
        first_cluster = ((uint32_t)target_entry->fst_clus_hi << 16) | target_entry->fst_clus_lo;
    }

    if (first_cluster < 2) {
        first_cluster = alloc_free_cluster(fs);
        if (first_cluster == 0) return -1; // Disk full
    }

    // Write data to cluster chain
    uint8_t *cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -1;

    size_t bytes_written = 0;
    uint32_t cur_cluster = first_cluster;
    uint32_t eof_val = (fs->type == FAT_TYPE_FAT32) ? 0x0FFFFFFF : 0xFFFF;

    while (bytes_written < len || bytes_written == 0) {
        memset(cluster_buf, 0, fs->bytes_per_cluster);
        size_t chunk = MIN(fs->bytes_per_cluster, len - bytes_written);
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

        uint32_t next = get_next_cluster(fs, cur_cluster);
        if (next < 2 || (fs->type == FAT_TYPE_FAT32 ? next >= 0x0FFFFFF8 : next >= 0xFFF8)) {
            next = alloc_free_cluster(fs);
            if (next == 0) {
                kfree(cluster_buf);
                return -1; // Disk full
            }
            set_fat_entry(fs, cur_cluster, next);
        }
        cur_cluster = next;
    }

    kfree(cluster_buf);

    // Update Directory Entry
    if (fs->bdev->read(fs->bdev, dir_sector, 1, sector) != 0) return -1;
    target_entry = &((fat_dir_entry_t*)sector)[dir_slot];
    memcpy(target_entry->name, fat83_name, 11);
    target_entry->attr = FAT_ATTR_ARCHIVE;
    target_entry->fst_clus_lo = (uint16_t)(first_cluster & 0xFFFF);
    target_entry->fst_clus_hi = (uint16_t)((first_cluster >> 16) & 0xFFFF);
    target_entry->file_size = (uint32_t)len;

    return fs->bdev->write(fs->bdev, dir_sector, 1, sector);
}

int fat_mkdir(fat_fs_t *fs, const char *dirname) {
    if (!fs || !dirname || dirname[0] == '\0') return -1;

    char fat83_name[11];
    to_fat_83_name(dirname, fat83_name);

    uint8_t sector[512];
    uint32_t dir_sector = 0;
    int dir_slot = -1;
    fat_dir_entry_t *target_entry = NULL;

    // 1. Search for existing entry or free slot in Root Directory
    if (fs->type == FAT_TYPE_FAT16 || fs->type == FAT_TYPE_FAT12) {
        uint32_t max_root_sectors = (fs->root_dir_sectors < 1024) ? fs->root_dir_sectors : 1024;
        for (uint32_t s = 0; s < max_root_sectors; s++) {
            if (fs->bdev->read(fs->bdev, fs->root_dir_start_sector + s, 1, sector) != 0) break;

            fat_dir_entry_t *entries = (fat_dir_entry_t*)sector;
            for (int e = 0; e < 16; e++) {
                if ((uint8_t)entries[e].name[0] == 0x00 || (uint8_t)entries[e].name[0] == 0xE5) {
                    if (dir_slot == -1) {
                        dir_sector = fs->root_dir_start_sector + s;
                        dir_slot = e;
                    }
                    if ((uint8_t)entries[e].name[0] == 0x00) goto search_mkdir_done;
                    continue;
                }
                if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) continue;

                char name[16];
                format_fat_name(entries[e].name, name);
                if (fat_name_equals(name, dirname)) {
                    return -2; // Directory or file already exists
                }
            }
        }
    } else if (fs->type == FAT_TYPE_FAT32) {
        uint32_t cur_clus = fs->root_cluster;
        if (cur_clus < 2 || fs->bytes_per_cluster == 0) return -1;
        uint8_t *cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
        if (!cluster_buf) return -1;

        uint32_t visited = 0;
        uint32_t max_clusters = (fs->total_clusters > 0 && fs->total_clusters < 65536) ? (fs->total_clusters + 10) : 65536;

        while (cur_clus >= 2 && cur_clus < 0x0FFFFFF8 && visited++ < max_clusters) {
            if (read_cluster(fs, cur_clus, cluster_buf) != 0) break;

            size_t entries_per_clus = fs->bytes_per_cluster / sizeof(fat_dir_entry_t);
            if (entries_per_clus == 0) break;
            fat_dir_entry_t *entries = (fat_dir_entry_t*)cluster_buf;

            for (size_t e = 0; e < entries_per_clus; e++) {
                if ((uint8_t)entries[e].name[0] == 0x00 || (uint8_t)entries[e].name[0] == 0xE5) {
                    if (dir_slot == -1) {
                        uint32_t sec_offset = (e * sizeof(fat_dir_entry_t)) / fs->bytes_per_sector;
                        dir_sector = fs->data_start_sector + ((cur_clus - 2) * fs->sectors_per_cluster) + sec_offset;
                        dir_slot = e % (fs->bytes_per_sector / sizeof(fat_dir_entry_t));
                    }
                    if ((uint8_t)entries[e].name[0] == 0x00) {
                        kfree(cluster_buf);
                        goto search_mkdir_done;
                    }
                    continue;
                }
                if (entries[e].attr == FAT_ATTR_LFN || (entries[e].attr & FAT_ATTR_VOLUME_ID)) continue;

                char name[16];
                format_fat_name(entries[e].name, name);
                if (fat_name_equals(name, dirname)) {
                    kfree(cluster_buf);
                    return -2; // Directory or file already exists
                }
            }
            uint32_t next = get_next_cluster(fs, cur_clus);
            if (next == cur_clus || next < 2) break;
            cur_clus = next;
        }
        kfree(cluster_buf);
    }

search_mkdir_done:
    if (dir_slot == -1) return -1; // Directory is full

    // 2. Allocate a cluster for the new directory
    uint32_t new_cluster = alloc_free_cluster(fs);
    if (new_cluster == 0) return -1; // Disk full

    // 3. Initialize directory contents: '.' and '..'
    uint8_t *cluster_buf = (uint8_t*)kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -1;
    memset(cluster_buf, 0, fs->bytes_per_cluster);

    fat_dir_entry_t *dot_entries = (fat_dir_entry_t*)cluster_buf;

    // '.' entry (self)
    memset(dot_entries[0].name, ' ', 11);
    dot_entries[0].name[0] = '.';
    dot_entries[0].attr = FAT_ATTR_DIRECTORY;
    dot_entries[0].fst_clus_lo = (uint16_t)(new_cluster & 0xFFFF);
    dot_entries[0].fst_clus_hi = (uint16_t)((new_cluster >> 16) & 0xFFFF);
    dot_entries[0].file_size = 0;

    // '..' entry (parent: root directory cluster is 0)
    memset(dot_entries[1].name, ' ', 11);
    dot_entries[1].name[0] = '.';
    dot_entries[1].name[1] = '.';
    dot_entries[1].attr = FAT_ATTR_DIRECTORY;
    dot_entries[1].fst_clus_lo = 0;
    dot_entries[1].fst_clus_hi = 0;
    dot_entries[1].file_size = 0;

    if (write_cluster(fs, new_cluster, cluster_buf) != 0) {
        kfree(cluster_buf);
        return -1;
    }
    kfree(cluster_buf);

    // 4. Update directory entry in parent (root) directory
    if (fs->bdev->read(fs->bdev, dir_sector, 1, sector) != 0) return -1;
    target_entry = &((fat_dir_entry_t*)sector)[dir_slot];
    memcpy(target_entry->name, fat83_name, 11);
    target_entry->attr = FAT_ATTR_DIRECTORY;
    target_entry->fst_clus_lo = (uint16_t)(new_cluster & 0xFFFF);
    target_entry->fst_clus_hi = (uint16_t)((new_cluster >> 16) & 0xFFFF);
    target_entry->file_size = 0;

    return fs->bdev->write(fs->bdev, dir_sector, 1, sector);
}
