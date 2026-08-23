/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS - Host Tool: mcopy replacement
 *
 * Copies files from host filesystem into a FAT12/FAT16/FAT32 disk image.
 * Standard C90 compliant.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_MSC_VER)
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
#else
#include <stdint.h>
#endif

#define SECTOR_SIZE 512

#pragma pack(push, 1)
struct fat_bpb {
    uint8_t  jmp_boot[3];
    char     oem_name[8];
    uint16_t bytes_per_sec;
    uint8_t  sec_per_clus;
    uint16_t rsvd_sec_cnt;
    uint8_t  num_fats;
    uint16_t root_ent_cnt;
    uint16_t tot_sec_16;
    uint8_t  media;
    uint16_t fat_sz_16;
    uint16_t sec_per_trk;
    uint16_t num_heads;
    uint32_t hidd_sec;
    uint32_t tot_sec_32;

    union {
        struct {
            uint8_t  drv_num;
            uint8_t  reserved1;
            uint8_t  boot_sig;
            uint32_t vol_id;
            char     vol_lab[11];
            char     fil_sys_type[8];
            uint8_t  boot_code[448];
            uint16_t signature;
        } fat16;

        struct {
            uint32_t fat_sz_32;
            uint16_t ext_flags;
            uint16_t fs_ver;
            uint32_t root_clus;
            uint16_t fs_info;
            uint16_t bk_boot_sec;
            uint8_t  reserved[12];
            uint8_t  drv_num;
            uint8_t  reserved1;
            uint8_t  boot_sig;
            uint32_t vol_id;
            char     vol_lab[11];
            char     fil_sys_type[8];
            uint8_t  boot_code[420];
            uint16_t signature;
        } fat32;
    } spec;
};

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
#pragma pack(pop)

static void get_dos_time(uint16_t *dos_time, uint16_t *dos_date) {
    time_t t;
    struct tm *tm;

    t = time(NULL);
    tm = localtime(&t);
    if (!tm) {
        *dos_time = 0;
        *dos_date = (2026 - 1980) << 9 | (1 << 5) | 1;
        return;
    }

    *dos_time = (uint16_t)(((tm->tm_hour & 0x1F) << 11) |
                           ((tm->tm_min & 0x3F) << 5) |
                           ((tm->tm_sec / 2) & 0x1F));

    *dos_date = (uint16_t)((((tm->tm_year + 1900 - 1980) & 0x7F) << 9) |
                           (((tm->tm_mon + 1) & 0x0F) << 5) |
                           (tm->tm_mday & 0x1F));
}

static void convert_to_83(const char *src, char *dst) {
    int i;
    const char *dot;
    int name_len;
    int ext_len;

    memset(dst, ' ', 11);

    /* Skip any leading drive/colons or path slashes */
    while (*src == ':' || *src == '/' || *src == '\\') src++;

    dot = strrchr(src, '.');
    if (dot) {
        name_len = (int)(dot - src);
        if (name_len > 8) name_len = 8;
        for (i = 0; i < name_len; i++) {
            char c = src[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            dst[i] = c;
        }

        dot++;
        ext_len = (int)strlen(dot);
        if (ext_len > 3) ext_len = 3;
        for (i = 0; i < ext_len; i++) {
            char c = dot[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            dst[8 + i] = c;
        }
    } else {
        name_len = (int)strlen(src);
        if (name_len > 8) name_len = 8;
        for (i = 0; i < name_len; i++) {
            char c = src[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            dst[i] = c;
        }
    }
}

static uint32_t fat_get_entry(const uint8_t *fat, int fat_type, uint32_t cluster) {
    if (fat_type == 12) {
        uint32_t offset = cluster + (cluster / 2);
        uint16_t val = (uint16_t)(fat[offset] | (fat[offset + 1] << 8));
        if (cluster & 1) {
            return val >> 4;
        } else {
            return val & 0x0FFF;
        }
    } else if (fat_type == 16) {
        const uint16_t *f16 = (const uint16_t*)(const void*)fat;
        return f16[cluster];
    } else {
        const uint32_t *f32 = (const uint32_t*)(const void*)fat;
        return f32[cluster] & 0x0FFFFFFF;
    }
}

static void fat_set_entry(uint8_t *fat, int fat_type, uint32_t cluster, uint32_t value) {
    if (fat_type == 12) {
        uint32_t offset = cluster + (cluster / 2);
        uint16_t val = (uint16_t)(fat[offset] | (fat[offset + 1] << 8));
        if (cluster & 1) {
            val = (uint16_t)((val & 0x000F) | ((value & 0x0FFF) << 4));
        } else {
            val = (uint16_t)((val & 0xF000) | (value & 0x0FFF));
        }
        fat[offset] = (uint8_t)(val & 0xFF);
        fat[offset + 1] = (uint8_t)((val >> 8) & 0xFF);
    } else if (fat_type == 16) {
        uint16_t *f16 = (uint16_t*)(void*)fat;
        f16[cluster] = (uint16_t)(value & 0xFFFF);
    } else {
        uint32_t *f32 = (uint32_t*)(void*)fat;
        f32[cluster] = (f32[cluster] & 0xF0000000) | (value & 0x0FFFFFFF);
    }
}

static uint32_t allocate_free_cluster(uint8_t *fat, int fat_type, uint32_t total_clusters) {
    uint32_t c;
    for (c = 2; c < total_clusters + 2; c++) {
        if (fat_get_entry(fat, fat_type, c) == 0) {
            uint32_t eoc = (fat_type == 12) ? 0x0FFF : (fat_type == 16 ? 0xFFFF : 0x0FFFFFFF);
            fat_set_entry(fat, fat_type, c, eoc);
            return c;
        }
    }
    return 0; /* Disk full */
}

static void free_cluster_chain(uint8_t *fat, int fat_type, uint32_t start_clus) {
    uint32_t cur = start_clus;
    uint32_t eoc_threshold = (fat_type == 12) ? 0x0FF8 : (fat_type == 16 ? 0xFFF8 : 0x0FFFFFF8);

    while (cur >= 2 && cur < eoc_threshold) {
        uint32_t next = fat_get_entry(fat, fat_type, cur);
        fat_set_entry(fat, fat_type, cur, 0);
        cur = next;
    }
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s -i <image_file> <source_file> ::<dest_filename>\n", prog);
}

int main(int argc, char **argv) {
    const char *image_path = NULL;
    const char *src_path = NULL;
    const char *dest_name = NULL;
    int i;
    FILE *f_img;
    FILE *f_src;
    long src_size;
    uint8_t *src_data;
    struct fat_bpb bpb;
    uint32_t fat_sz;
    uint32_t tot_sec;
    uint32_t root_dir_sec;
    uint32_t data_sec;
    uint32_t total_clusters;
    int fat_type;
    size_t fat_bytes;
    uint8_t *fat_buf;
    char target_83[11];
    uint32_t cluster_size;
    uint32_t clusters_needed;
    uint32_t first_cluster;
    uint32_t prev_cluster;
    uint32_t clus_idx;
    uint32_t first_data_sec;
    uint16_t dos_time;
    uint16_t dos_date;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            image_path = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else if (!src_path) {
            src_path = argv[i];
        } else if (!dest_name) {
            dest_name = argv[i];
        }
    }

    if (!image_path || !src_path || !dest_name) {
        print_usage(argv[0]);
        return 1;
    }

    /* Clean destination filename */
    while (*dest_name == ':') dest_name++;
    convert_to_83(dest_name, target_83);

    /* Read source file from host filesystem */
    f_src = fopen(src_path, "rb");
    if (!f_src) {
        perror("fopen src");
        return 1;
    }
    fseek(f_src, 0, SEEK_END);
    src_size = ftell(f_src);
    fseek(f_src, 0, SEEK_SET);

    src_data = NULL;
    if (src_size > 0) {
        src_data = (uint8_t*)malloc(src_size);
        if (!src_data) {
            perror("malloc src");
            fclose(f_src);
            return 1;
        }
        if (fread(src_data, 1, src_size, f_src) != (size_t)src_size) {
            perror("fread src");
            free(src_data);
            fclose(f_src);
            return 1;
        }
    }
    fclose(f_src);

    /* Open disk image */
    f_img = fopen(image_path, "r+b");
    if (!f_img) {
        perror("fopen img");
        if (src_data) free(src_data);
        return 1;
    }

    /* Read BPB */
    fseek(f_img, 0, SEEK_SET);
    if (fread(&bpb, 1, sizeof(bpb), f_img) != sizeof(bpb)) {
        perror("fread bpb");
        fclose(f_img);
        if (src_data) free(src_data);
        return 1;
    }

    if (bpb.bytes_per_sec != SECTOR_SIZE) {
        fprintf(stderr, "Error: Invalid sector size %u\n", bpb.bytes_per_sec);
        fclose(f_img);
        if (src_data) free(src_data);
        return 1;
    }

    root_dir_sec = ((bpb.root_ent_cnt * 32) + (SECTOR_SIZE - 1)) / SECTOR_SIZE;
    fat_sz = (bpb.fat_sz_16 != 0) ? bpb.fat_sz_16 : bpb.spec.fat32.fat_sz_32;
    tot_sec = (bpb.tot_sec_16 != 0) ? bpb.tot_sec_16 : bpb.tot_sec_32;
    data_sec = tot_sec - (bpb.rsvd_sec_cnt + (bpb.num_fats * fat_sz) + root_dir_sec);
    total_clusters = data_sec / bpb.sec_per_clus;

    if (total_clusters < 4085) {
        fat_type = 12;
    } else if (total_clusters < 65525) {
        fat_type = 16;
    } else {
        fat_type = 32;
    }

    cluster_size = bpb.sec_per_clus * SECTOR_SIZE;
    first_data_sec = bpb.rsvd_sec_cnt + (bpb.num_fats * fat_sz) + root_dir_sec;

    /* Read FAT1 into memory */
    fat_bytes = fat_sz * SECTOR_SIZE;
    fat_buf = (uint8_t*)malloc(fat_bytes);
    if (!fat_buf) {
        perror("malloc fat");
        fclose(f_img);
        if (src_data) free(src_data);
        return 1;
    }

    fseek(f_img, (long)bpb.rsvd_sec_cnt * SECTOR_SIZE, SEEK_SET);
    if (fread(fat_buf, 1, fat_bytes, f_img) != fat_bytes) {
        perror("fread fat");
        free(fat_buf);
        fclose(f_img);
        if (src_data) free(src_data);
        return 1;
    }

    /* Allocate clusters for file data */
    clusters_needed = (src_size > 0) ? (uint32_t)((src_size + cluster_size - 1) / cluster_size) : 0;
    first_cluster = 0;
    prev_cluster = 0;

    for (clus_idx = 0; clus_idx < clusters_needed; clus_idx++) {
        uint32_t c;
        c = allocate_free_cluster(fat_buf, fat_type, total_clusters);
        if (c == 0) {
            fprintf(stderr, "Error: Disk full while writing %s\n", dest_name);
            if (first_cluster != 0) {
                free_cluster_chain(fat_buf, fat_type, first_cluster);
            }
            free(fat_buf);
            fclose(f_img);
            if (src_data) free(src_data);
            return 1;
        }

        if (first_cluster == 0) {
            first_cluster = c;
        } else {
            fat_set_entry(fat_buf, fat_type, prev_cluster, c);
        }
        prev_cluster = c;

        /* Write cluster data */
        {
            uint32_t sec_offset;
            long data_pos;
            size_t bytes_to_copy;
            uint8_t *clus_buf;

            sec_offset = first_data_sec + (c - 2) * bpb.sec_per_clus;
            data_pos = (long)sec_offset * SECTOR_SIZE;

            bytes_to_copy = src_size - (clus_idx * cluster_size);
            if (bytes_to_copy > cluster_size) bytes_to_copy = cluster_size;

            clus_buf = (uint8_t*)calloc(1, cluster_size);
            if (bytes_to_copy > 0 && src_data) {
                memcpy(clus_buf, src_data + (clus_idx * cluster_size), bytes_to_copy);
            }

            fseek(f_img, data_pos, SEEK_SET);
            fwrite(clus_buf, 1, cluster_size, f_img);
            free(clus_buf);
        }
    }

    if (prev_cluster != 0) {
        uint32_t eoc = (fat_type == 12) ? 0x0FFF : (fat_type == 16 ? 0xFFFF : 0x0FFFFFFF);
        fat_set_entry(fat_buf, fat_type, prev_cluster, eoc);
    }

    /* Write updated FAT1 and FAT2 back to disk */
    fseek(f_img, (long)bpb.rsvd_sec_cnt * SECTOR_SIZE, SEEK_SET);
    fwrite(fat_buf, 1, fat_bytes, f_img);
    if (bpb.num_fats > 1) {
        fseek(f_img, (long)(bpb.rsvd_sec_cnt + fat_sz) * SECTOR_SIZE, SEEK_SET);
        fwrite(fat_buf, 1, fat_bytes, f_img);
    }

    /* Update Root Directory Entry */
    get_dos_time(&dos_time, &dos_date);

    if (fat_type == 12 || fat_type == 16) {
        /* FAT12 / FAT16 Fixed Root Directory */
        size_t root_dir_bytes = root_dir_sec * SECTOR_SIZE;
        uint8_t *root_buf = (uint8_t*)malloc(root_dir_bytes);
        long root_pos = (long)(bpb.rsvd_sec_cnt + bpb.num_fats * fat_sz) * SECTOR_SIZE;
        struct fat_dir_entry *entries;
        size_t max_entries;
        size_t ent_idx;
        int found_slot = -1;

        if (!root_buf) {
            perror("malloc root_buf");
            free(fat_buf);
            fclose(f_img);
            if (src_data) free(src_data);
            return 1;
        }

        fseek(f_img, root_pos, SEEK_SET);
        if (fread(root_buf, 1, root_dir_bytes, f_img) != root_dir_bytes) {
            perror("fread root_buf");
            free(root_buf);
            free(fat_buf);
            fclose(f_img);
            if (src_data) free(src_data);
            return 1;
        }

        entries = (struct fat_dir_entry*)(void*)root_buf;
        max_entries = root_dir_bytes / sizeof(struct fat_dir_entry);

        for (ent_idx = 0; ent_idx < max_entries; ent_idx++) {
            if (entries[ent_idx].name[0] == 0x00 || (uint8_t)entries[ent_idx].name[0] == 0xE5) {
                if (found_slot < 0) found_slot = (int)ent_idx;
            } else if (memcmp(entries[ent_idx].name, target_83, 11) == 0) {
                /* Existing file: overwrite */
                uint32_t old_clus = entries[ent_idx].fst_clus_lo | ((uint32_t)entries[ent_idx].fst_clus_hi << 16);
                if (old_clus != 0) {
                    free_cluster_chain(fat_buf, fat_type, old_clus);
                }
                found_slot = (int)ent_idx;
                break;
            }
        }

        if (found_slot < 0) {
            fprintf(stderr, "Error: Root directory is full!\n");
            free(root_buf);
            free(fat_buf);
            fclose(f_img);
            if (src_data) free(src_data);
            return 1;
        }

        memset(&entries[found_slot], 0, sizeof(struct fat_dir_entry));
        memcpy(entries[found_slot].name, target_83, 11);
        entries[found_slot].attr = 0x20; /* Archive */
        entries[found_slot].crt_time = dos_time;
        entries[found_slot].crt_date = dos_date;
        entries[found_slot].wrt_time = dos_time;
        entries[found_slot].wrt_date = dos_date;
        entries[found_slot].fst_clus_lo = (uint16_t)(first_cluster & 0xFFFF);
        entries[found_slot].fst_clus_hi = (uint16_t)((first_cluster >> 16) & 0xFFFF);
        entries[found_slot].file_size = (uint32_t)src_size;

        fseek(f_img, root_pos, SEEK_SET);
        fwrite(root_buf, 1, root_dir_bytes, f_img);
        free(root_buf);
    } else {
        /* FAT32 Root Directory (Cluster Chain starting at root_clus) */
        uint32_t root_clus = bpb.spec.fat32.root_clus;
        uint8_t *clus_buf = (uint8_t*)malloc(cluster_size);
        int entry_written = 0;
        uint32_t cur_clus = root_clus;
        uint32_t eoc_thresh = 0x0FFFFFF8;

        if (!clus_buf) {
            perror("malloc clus_buf");
            free(fat_buf);
            fclose(f_img);
            if (src_data) free(src_data);
            return 1;
        }

        while (cur_clus >= 2 && cur_clus < eoc_thresh && !entry_written) {
            long clus_pos;
            struct fat_dir_entry *entries;
            size_t max_entries;
            size_t ent_idx;

            clus_pos = (long)(first_data_sec + (cur_clus - 2) * bpb.sec_per_clus) * SECTOR_SIZE;
            fseek(f_img, clus_pos, SEEK_SET);
            fread(clus_buf, 1, cluster_size, f_img);

            entries = (struct fat_dir_entry*)(void*)clus_buf;
            max_entries = cluster_size / sizeof(struct fat_dir_entry);

            for (ent_idx = 0; ent_idx < max_entries; ent_idx++) {
                if (entries[ent_idx].name[0] == 0x00 || (uint8_t)entries[ent_idx].name[0] == 0xE5) {
                    memset(&entries[ent_idx], 0, sizeof(struct fat_dir_entry));
                    memcpy(entries[ent_idx].name, target_83, 11);
                    entries[ent_idx].attr = 0x20;
                    entries[ent_idx].crt_time = dos_time;
                    entries[ent_idx].crt_date = dos_date;
                    entries[ent_idx].wrt_time = dos_time;
                    entries[ent_idx].wrt_date = dos_date;
                    entries[ent_idx].fst_clus_lo = (uint16_t)(first_cluster & 0xFFFF);
                    entries[ent_idx].fst_clus_hi = (uint16_t)((first_cluster >> 16) & 0xFFFF);
                    entries[ent_idx].file_size = (uint32_t)src_size;

                    fseek(f_img, clus_pos, SEEK_SET);
                    fwrite(clus_buf, 1, cluster_size, f_img);
                    entry_written = 1;
                    break;
                } else if (memcmp(entries[ent_idx].name, target_83, 11) == 0) {
                    /* Overwrite */
                    uint32_t old_clus = entries[ent_idx].fst_clus_lo | ((uint32_t)entries[ent_idx].fst_clus_hi << 16);
                    if (old_clus != 0) {
                        free_cluster_chain(fat_buf, fat_type, old_clus);
                    }
                    memset(&entries[ent_idx], 0, sizeof(struct fat_dir_entry));
                    memcpy(entries[ent_idx].name, target_83, 11);
                    entries[ent_idx].attr = 0x20;
                    entries[ent_idx].crt_time = dos_time;
                    entries[ent_idx].crt_date = dos_date;
                    entries[ent_idx].wrt_time = dos_time;
                    entries[ent_idx].wrt_date = dos_date;
                    entries[ent_idx].fst_clus_lo = (uint16_t)(first_cluster & 0xFFFF);
                    entries[ent_idx].fst_clus_hi = (uint16_t)((first_cluster >> 16) & 0xFFFF);
                    entries[ent_idx].file_size = (uint32_t)src_size;

                    fseek(f_img, clus_pos, SEEK_SET);
                    fwrite(clus_buf, 1, cluster_size, f_img);
                    entry_written = 1;
                    break;
                }
            }

            if (!entry_written) {
                uint32_t next = fat_get_entry(fat_buf, fat_type, cur_clus);
                if (next >= eoc_thresh) {
                    /* Extend directory by 1 cluster */
                    uint32_t new_c = allocate_free_cluster(fat_buf, fat_type, total_clusters);
                    if (new_c == 0) {
                        fprintf(stderr, "Error: Disk full while extending root directory!\n");
                        break;
                    }
                    fat_set_entry(fat_buf, fat_type, cur_clus, new_c);
                    fat_set_entry(fat_buf, fat_type, new_c, 0x0FFFFFFF);

                    /* Zero new cluster */
                    memset(clus_buf, 0, cluster_size);
                    clus_pos = (long)(first_data_sec + (new_c - 2) * bpb.sec_per_clus) * SECTOR_SIZE;
                    fseek(f_img, clus_pos, SEEK_SET);
                    fwrite(clus_buf, 1, cluster_size, f_img);

                    /* Write entry to first slot of new cluster */
                    entries = (struct fat_dir_entry*)(void*)clus_buf;
                    memcpy(entries[0].name, target_83, 11);
                    entries[0].attr = 0x20;
                    entries[0].crt_time = dos_time;
                    entries[0].crt_date = dos_date;
                    entries[0].wrt_time = dos_time;
                    entries[0].wrt_date = dos_date;
                    entries[0].fst_clus_lo = (uint16_t)(first_cluster & 0xFFFF);
                    entries[0].fst_clus_hi = (uint16_t)((first_cluster >> 16) & 0xFFFF);
                    entries[0].file_size = (uint32_t)src_size;

                    fseek(f_img, clus_pos, SEEK_SET);
                    fwrite(clus_buf, 1, cluster_size, f_img);
                    entry_written = 1;
                }
                cur_clus = next;
            }
        }

        free(clus_buf);
    }

    free(fat_buf);
    fclose(f_img);
    if (src_data) free(src_data);

    printf("Copied %s -> %s (%.11s) (%ld bytes, cluster %u)\n",
           src_path, dest_name, target_83, src_size, first_cluster);

    return 0;
}
