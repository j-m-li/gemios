/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS - Host Tool: mcopy replacement with Long File Name (VFAT) Support
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

struct fat_lfn_entry {
    uint8_t  order;
    uint8_t  name1[10];
    uint8_t  attr;
    uint8_t  type;
    uint8_t  checksum;
    uint8_t  name2[12];
    uint8_t  fst_clus_lo[2];
    uint8_t  name3[4];
};

static uint16_t rd_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

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

static uint8_t calc_lfn_checksum(const char *short_name) {
    uint8_t sum = 0;
    int i;
    for (i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + (uint8_t)short_name[i]);
    }
    return sum;
}

static char to_upper_char(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    return c;
}

static int needs_lfn(const char *name) {
    int i;
    int base_len;
    int ext_len;
    const char *dot;
    size_t total_len;

    if (!name) return 0;
    total_len = strlen(name);
    if (total_len > 12) return 1;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;

    dot = NULL;
    for (i = 0; name[i]; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') return 1;
        if (c == ' ' || c == '+' || c == ',' || c == ';' || c == '=' || c == '[' || c == ']') return 1;
        if (c == '.') {
            if (dot != NULL) return 1;
            dot = &name[i];
        }
    }

    if (!dot) {
        if (total_len > 8) return 1;
    } else {
        base_len = (int)(dot - name);
        ext_len = (int)(total_len - base_len - 1);
        if (base_len > 8 || ext_len > 3 || base_len == 0) return 1;
    }

    return 0;
}

static void convert_to_83(const char *src, char *dst) {
    char base[9];
    char ext[4];
    const char *last_dot;
    int b, e, i;

    memset(dst, ' ', 11);
    memset(base, 0, sizeof(base));
    memset(ext, 0, sizeof(ext));

    /* Skip leading colons or slashes */
    while (*src == ':' || *src == '/' || *src == '\\') src++;

    last_dot = strrchr(src, '.');

    b = 0;
    for (i = 0; src[i] && (last_dot == NULL || &src[i] < last_dot) && b < 8; i++) {
        char c = to_upper_char(src[i]);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            base[b++] = c;
        }
    }
    if (b == 0) {
        strcpy(base, "FILE");
        b = 4;
    }

    if (last_dot) {
        e = 0;
        for (i = 1; last_dot[i] && e < 3; i++) {
            char c = to_upper_char(last_dot[i]);
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
                ext[e++] = c;
            }
        }
    }

    if (needs_lfn(src)) {
        if (b > 6) b = 6;
        base[b] = '\0';
        strcat(base, "~1");
    }

    for (i = 0; base[i] && i < 8; i++) {
        dst[i] = base[i];
    }
    for (i = 0; ext[i] && i < 3; i++) {
        dst[8 + i] = ext[i];
    }
}

static void encode_lfn_chars(const char *src, int src_len, int start_idx, uint8_t *dst, int count) {
    int i;
    for (i = 0; i < count; i++) {
        int idx = start_idx + i;
        uint16_t u;
        if (idx < src_len) {
            u = (uint16_t)(uint8_t)src[idx];
        } else if (idx == src_len) {
            u = 0x0000;
        } else {
            u = 0xFFFF;
        }
        dst[i * 2] = (uint8_t)(u & 0xFF);
        dst[i * 2 + 1] = (uint8_t)((u >> 8) & 0xFF);
    }
}

static void write_lfn_entries(struct fat_dir_entry *entries, int start_slot, const char *long_name, const char *short_11, int num_lfn) {
    int name_len = (int)strlen(long_name);
    uint8_t chk = calc_lfn_checksum(short_11);
    int seq;
    int cur_slot = start_slot;

    for (seq = num_lfn; seq >= 1; seq--) {
        struct fat_lfn_entry *lfn = (struct fat_lfn_entry*)(void*)&entries[cur_slot];
        memset(lfn, 0, sizeof(struct fat_lfn_entry));
        lfn->order = (seq == num_lfn) ? (0x40 | seq) : seq;
        lfn->attr = 0x0F;
        lfn->type = 0;
        lfn->checksum = chk;
        lfn->fst_clus_lo[0] = 0;
        lfn->fst_clus_lo[1] = 0;

        encode_lfn_chars(long_name, name_len, (seq - 1) * 13 + 0, lfn->name1, 5);
        encode_lfn_chars(long_name, name_len, (seq - 1) * 13 + 5, lfn->name2, 6);
        encode_lfn_chars(long_name, name_len, (seq - 1) * 13 + 11, lfn->name3, 2);

        cur_slot++;
    }
}

static uint32_t fat_get_entry(const uint8_t *fat_buf, int fat_type, uint32_t cluster) {
    if (fat_type == 12) {
        uint32_t offset = cluster + (cluster / 2);
        uint16_t val = (uint16_t)fat_buf[offset] | ((uint16_t)fat_buf[offset + 1] << 8);
        if (cluster & 1) return val >> 4;
        else return val & 0x0FFF;
    } else if (fat_type == 16) {
        uint32_t offset = cluster * 2;
        return (uint32_t)rd_le16(&fat_buf[offset]);
    } else {
        uint32_t offset = cluster * 4;
        return rd_le32(&fat_buf[offset]) & 0x0FFFFFFF;
    }
}

static void fat_set_entry(uint8_t *fat_buf, int fat_type, uint32_t cluster, uint32_t val) {
    if (fat_type == 12) {
        uint32_t offset = cluster + (cluster / 2);
        uint16_t cur = (uint16_t)fat_buf[offset] | ((uint16_t)fat_buf[offset + 1] << 8);
        if (cluster & 1) {
            cur = (cur & 0x000F) | ((val & 0x0FFF) << 4);
        } else {
            cur = (cur & 0xF000) | (val & 0x0FFF);
        }
        fat_buf[offset] = (uint8_t)(cur & 0xFF);
        fat_buf[offset + 1] = (uint8_t)((cur >> 8) & 0xFF);
    } else if (fat_type == 16) {
        uint32_t offset = cluster * 2;
        fat_buf[offset] = (uint8_t)(val & 0xFF);
        fat_buf[offset + 1] = (uint8_t)((val >> 8) & 0xFF);
    } else {
        uint32_t offset = cluster * 4;
        uint32_t orig = rd_le32(&fat_buf[offset]);
        uint32_t nw = (orig & 0xF0000000) | (val & 0x0FFFFFFF);
        fat_buf[offset] = (uint8_t)(nw & 0xFF);
        fat_buf[offset + 1] = (uint8_t)((nw >> 8) & 0xFF);
        fat_buf[offset + 2] = (uint8_t)((nw >> 16) & 0xFF);
        fat_buf[offset + 3] = (uint8_t)((nw >> 24) & 0xFF);
    }
}

static uint32_t allocate_free_cluster(uint8_t *fat_buf, int fat_type, uint32_t total_clusters) {
    uint32_t c;
    for (c = 2; c < total_clusters + 2; c++) {
        if (fat_get_entry(fat_buf, fat_type, c) == 0) {
            return c;
        }
    }
    return 0;
}

static void free_cluster_chain(uint8_t *fat_buf, int fat_type, uint32_t start_cluster) {
    uint32_t c = start_cluster;
    uint32_t eoc = (fat_type == 12) ? 0x0FF8 : (fat_type == 16 ? 0xFFF8 : 0x0FFFFFF8);
    uint32_t visited = 0;

    while (c >= 2 && c < eoc && visited++ < 65536) {
        uint32_t next = fat_get_entry(fat_buf, fat_type, c);
        fat_set_entry(fat_buf, fat_type, c, 0);
        c = next;
    }
}

int main(int argc, char **argv) {
    const char *img_path;
    const char *src_path;
    const char *dest_name;
    FILE *f_img;
    FILE *f_src;
    uint8_t bpb[SECTOR_SIZE];
    uint16_t bytes_per_sec;
    uint8_t sec_per_clus;
    uint16_t rsvd_sec_cnt;
    uint8_t num_fats;
    uint16_t root_ent_cnt;
    uint16_t tot_sec_16;
    uint32_t tot_sec_32;
    uint32_t fat_sz;
    uint32_t root_clus;
    uint32_t total_sec;
    uint32_t root_dir_sec;
    uint32_t data_sec;
    uint32_t total_clusters;
    int fat_type;
    uint32_t cluster_size;
    uint32_t first_data_sec;
    uint8_t *src_data;
    long src_size;
    uint8_t *fat_buf;
    size_t fat_bytes;
    uint32_t clusters_needed;
    uint32_t first_cluster;
    uint32_t prev_cluster;
    uint32_t clus_idx;
    uint16_t dos_time, dos_date;
    char target_83[11];
    int num_lfn_slots;
    int total_needed_slots;

    if (argc < 4) {
        fprintf(stderr, "Usage: %s -i <image.img> <src_file> ::<dest_filename>\n", argv[0]);
        return 1;
    }

    img_path = argv[2];
    src_path = argv[3];
    dest_name = (argc >= 5) ? argv[4] : argv[3];

    while (*dest_name == ':' || *dest_name == '/' || *dest_name == '\\') dest_name++;
    if (strlen(dest_name) == 0) dest_name = "FILE.TXT";

    num_lfn_slots = needs_lfn(dest_name) ? ((int)(strlen(dest_name) + 12) / 13) : 0;
    total_needed_slots = num_lfn_slots + 1;

    convert_to_83(dest_name, target_83);

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

    f_img = fopen(img_path, "r+b");
    if (!f_img) {
        perror("fopen img");
        if (src_data) free(src_data);
        return 1;
    }

    if (fread(bpb, 1, SECTOR_SIZE, f_img) != SECTOR_SIZE) {
        perror("fread bpb");
        fclose(f_img);
        if (src_data) free(src_data);
        return 1;
    }

    bytes_per_sec = rd_le16(&bpb[11]);
    sec_per_clus = bpb[13];
    rsvd_sec_cnt = rd_le16(&bpb[14]);
    num_fats = bpb[16];
    root_ent_cnt = rd_le16(&bpb[17]);
    tot_sec_16 = rd_le16(&bpb[19]);
    tot_sec_32 = rd_le32(&bpb[32]);
    total_sec = (tot_sec_16 != 0) ? tot_sec_16 : tot_sec_32;

    fat_sz = rd_le16(&bpb[22]);
    if (fat_sz == 0) {
        fat_sz = rd_le32(&bpb[36]);
        root_clus = rd_le32(&bpb[44]);
    } else {
        root_clus = 0;
    }

    root_dir_sec = (((uint32_t)root_ent_cnt * 32) + (bytes_per_sec - 1)) / bytes_per_sec;
    data_sec = total_sec - (rsvd_sec_cnt + (num_fats * fat_sz) + root_dir_sec);
    total_clusters = data_sec / sec_per_clus;

    if (total_clusters < 4085) {
        fat_type = 12;
    } else if (total_clusters < 65525) {
        fat_type = 16;
    } else {
        fat_type = 32;
    }

    cluster_size = sec_per_clus * SECTOR_SIZE;
    first_data_sec = rsvd_sec_cnt + (num_fats * fat_sz) + root_dir_sec;

    fat_bytes = fat_sz * SECTOR_SIZE;
    fat_buf = (uint8_t*)malloc(fat_bytes);
    if (!fat_buf) {
        perror("malloc fat");
        fclose(f_img);
        if (src_data) free(src_data);
        return 1;
    }

    fseek(f_img, (long)rsvd_sec_cnt * SECTOR_SIZE, SEEK_SET);
    if (fread(fat_buf, 1, fat_bytes, f_img) != fat_bytes) {
        perror("fread fat");
        free(fat_buf);
        fclose(f_img);
        if (src_data) free(src_data);
        return 1;
    }

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

        {
            uint32_t sec_offset;
            long data_pos;
            size_t bytes_to_copy;
            uint8_t *clus_buf;

            sec_offset = first_data_sec + (c - 2) * sec_per_clus;
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

    fseek(f_img, (long)rsvd_sec_cnt * SECTOR_SIZE, SEEK_SET);
    fwrite(fat_buf, 1, fat_bytes, f_img);
    if (num_fats > 1) {
        fseek(f_img, (long)(rsvd_sec_cnt + fat_sz) * SECTOR_SIZE, SEEK_SET);
        fwrite(fat_buf, 1, fat_bytes, f_img);
    }

    get_dos_time(&dos_time, &dos_date);

    if (fat_type == 12 || fat_type == 16) {
        size_t root_dir_bytes = root_dir_sec * SECTOR_SIZE;
        uint8_t *root_buf = (uint8_t*)malloc(root_dir_bytes);
        long root_pos = (long)(rsvd_sec_cnt + num_fats * fat_sz) * SECTOR_SIZE;
        struct fat_dir_entry *entries;
        size_t max_entries;
        size_t ent_idx;
        int run_start = -1;
        int run_count = 0;

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
                if (run_count == 0) run_start = (int)ent_idx;
                run_count++;
                if (run_count >= total_needed_slots) break;
            } else {
                run_count = 0;
            }
        }

        if (run_count < total_needed_slots) {
            fprintf(stderr, "Error: Root directory is full!\n");
            free(root_buf);
            free(fat_buf);
            fclose(f_img);
            if (src_data) free(src_data);
            return 1;
        }

        if (num_lfn_slots > 0) {
            write_lfn_entries(entries, run_start, dest_name, target_83, num_lfn_slots);
        }

        {
            int short_slot = run_start + num_lfn_slots;
            memset(&entries[short_slot], 0, sizeof(struct fat_dir_entry));
            memcpy(entries[short_slot].name, target_83, 11);
            entries[short_slot].attr = 0x20;
            entries[short_slot].crt_time = dos_time;
            entries[short_slot].crt_date = dos_date;
            entries[short_slot].wrt_time = dos_time;
            entries[short_slot].wrt_date = dos_date;
            entries[short_slot].fst_clus_lo = (uint16_t)(first_cluster & 0xFFFF);
            entries[short_slot].fst_clus_hi = (uint16_t)((first_cluster >> 16) & 0xFFFF);
            entries[short_slot].file_size = (uint32_t)src_size;
        }

        fseek(f_img, root_pos, SEEK_SET);
        fwrite(root_buf, 1, root_dir_bytes, f_img);
        free(root_buf);
    } else {
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
            int run_start = -1;
            int run_count = 0;

            clus_pos = (long)(first_data_sec + (cur_clus - 2) * sec_per_clus) * SECTOR_SIZE;
            fseek(f_img, clus_pos, SEEK_SET);
            fread(clus_buf, 1, cluster_size, f_img);

            entries = (struct fat_dir_entry*)(void*)clus_buf;
            max_entries = cluster_size / sizeof(struct fat_dir_entry);

            for (ent_idx = 0; ent_idx < max_entries; ent_idx++) {
                if (entries[ent_idx].name[0] == 0x00 || (uint8_t)entries[ent_idx].name[0] == 0xE5) {
                    if (run_count == 0) run_start = (int)ent_idx;
                    run_count++;
                    if (run_count >= total_needed_slots) {
                        if (num_lfn_slots > 0) {
                            write_lfn_entries(entries, run_start, dest_name, target_83, num_lfn_slots);
                        }
                        {
                            int short_slot = run_start + num_lfn_slots;
                            memset(&entries[short_slot], 0, sizeof(struct fat_dir_entry));
                            memcpy(entries[short_slot].name, target_83, 11);
                            entries[short_slot].attr = 0x20;
                            entries[short_slot].crt_time = dos_time;
                            entries[short_slot].crt_date = dos_date;
                            entries[short_slot].wrt_time = dos_time;
                            entries[short_slot].wrt_date = dos_date;
                            entries[short_slot].fst_clus_lo = (uint16_t)(first_cluster & 0xFFFF);
                            entries[short_slot].fst_clus_hi = (uint16_t)((first_cluster >> 16) & 0xFFFF);
                            entries[short_slot].file_size = (uint32_t)src_size;
                        }

                        fseek(f_img, clus_pos, SEEK_SET);
                        fwrite(clus_buf, 1, cluster_size, f_img);
                        entry_written = 1;
                        break;
                    }
                } else {
                    run_count = 0;
                }
            }

            if (!entry_written) {
                uint32_t next = fat_get_entry(fat_buf, fat_type, cur_clus);
                if (next >= eoc_thresh) {
                    uint32_t new_c = allocate_free_cluster(fat_buf, fat_type, total_clusters);
                    if (new_c == 0) {
                        fprintf(stderr, "Error: Disk full while extending root directory!\n");
                        break;
                    }
                    fat_set_entry(fat_buf, fat_type, cur_clus, new_c);
                    fat_set_entry(fat_buf, fat_type, new_c, 0x0FFFFFFF);

                    memset(clus_buf, 0, cluster_size);
                    clus_pos = (long)(first_data_sec + (new_c - 2) * sec_per_clus) * SECTOR_SIZE;
                    fseek(f_img, clus_pos, SEEK_SET);
                    fwrite(clus_buf, 1, cluster_size, f_img);

                    entries = (struct fat_dir_entry*)(void*)clus_buf;
                    if (num_lfn_slots > 0) {
                        write_lfn_entries(entries, 0, dest_name, target_83, num_lfn_slots);
                    }
                    {
                        int short_slot = num_lfn_slots;
                        memset(&entries[short_slot], 0, sizeof(struct fat_dir_entry));
                        memcpy(entries[short_slot].name, target_83, 11);
                        entries[short_slot].attr = 0x20;
                        entries[short_slot].crt_time = dos_time;
                        entries[short_slot].crt_date = dos_date;
                        entries[short_slot].wrt_time = dos_time;
                        entries[short_slot].wrt_date = dos_date;
                        entries[short_slot].fst_clus_lo = (uint16_t)(first_cluster & 0xFFFF);
                        entries[short_slot].fst_clus_hi = (uint16_t)((first_cluster >> 16) & 0xFFFF);
                        entries[short_slot].file_size = (uint32_t)src_size;
                    }

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
