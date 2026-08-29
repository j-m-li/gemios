/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS - Host Tool: mkfs.fat replacement
 *
 * Formats a disk image file with FAT12, FAT16, or FAT32 filesystem.
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

static void wr_le16(uint8_t *p, uint16_t val) {
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
}

static void wr_le32(uint8_t *p, uint32_t val) {
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    p[2] = (uint8_t)((val >> 16) & 0xFF);
    p[3] = (uint8_t)((val >> 24) & 0xFF);
}

struct fat32_fsinfo {
    uint32_t lead_sig;
    uint8_t  reserved1[480];
    uint32_t struc_sig;
    uint32_t free_count;
    uint32_t nxt_free;
    uint8_t  reserved2[12];
    uint32_t trail_sig;
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

static void format_volume_label(const char *src, char *dst) {
    size_t i;
    size_t len;

    memset(dst, ' ', 11);
    if (!src) return;

    len = strlen(src);
    for (i = 0; i < 11 && i < len; i++) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        dst[i] = c;
    }
}

static void make_bpb(uint8_t *sec, int fat_type, uint32_t total_sectors, uint8_t sec_per_clus,
                     uint16_t rsvd_sec_cnt, uint16_t root_ent_cnt, uint32_t fat_size,
                     uint32_t vol_id, const char *vol_name) {
    memset(sec, 0, SECTOR_SIZE);
    sec[0] = 0xEB;
    sec[1] = (fat_type == 32) ? 0x58 : 0x3C;
    sec[2] = 0x90;
    memcpy(&sec[3], "GEMIOS  ", 8);
    wr_le16(&sec[11], SECTOR_SIZE);
    sec[13] = sec_per_clus;
    wr_le16(&sec[14], rsvd_sec_cnt);
    sec[16] = 2; /* num_fats */
    wr_le16(&sec[17], root_ent_cnt);
    if (fat_type == 32 || total_sectors >= 65536) {
        wr_le16(&sec[19], 0);
        wr_le32(&sec[32], total_sectors);
    } else {
        wr_le16(&sec[19], (uint16_t)total_sectors);
        wr_le32(&sec[32], 0);
    }
    sec[21] = 0xF8; /* media */
    wr_le16(&sec[22], (fat_type == 32) ? 0 : (uint16_t)fat_size);
    wr_le16(&sec[24], 32); /* sec_per_trk */
    wr_le16(&sec[26], 64); /* num_heads */
    wr_le32(&sec[28], 0);  /* hidd_sec */

    if (fat_type == 12 || fat_type == 16) {
        sec[36] = 0x80; /* drv_num */
        sec[37] = 0;    /* reserved1 */
        sec[38] = 0x29; /* boot_sig */
        wr_le32(&sec[39], vol_id);
        format_volume_label(vol_name ? vol_name : (fat_type == 12 ? "GEMIOS12" : "GEMIOS16"), (char*)&sec[43]);
        memcpy(&sec[54], (fat_type == 12 ? "FAT12   " : "FAT16   "), 8);
        sec[510] = 0x55;
        sec[511] = 0xAA;
    } else {
        wr_le32(&sec[36], fat_size);
        wr_le16(&sec[40], 0); /* ext_flags */
        wr_le16(&sec[42], 0); /* fs_ver */
        wr_le32(&sec[44], 2); /* root_clus */
        wr_le16(&sec[48], 1); /* fs_info */
        wr_le16(&sec[50], 6); /* bk_boot_sec */
        sec[64] = 0x80;       /* drv_num */
        sec[65] = 0;          /* reserved1 */
        sec[66] = 0x29;       /* boot_sig */
        wr_le32(&sec[67], vol_id);
        format_volume_label(vol_name ? vol_name : "GEMIOS32", (char*)&sec[71]);
        memcpy(&sec[82], "FAT32   ", 8);
        sec[510] = 0x55;
        sec[511] = 0xAA;
    }
}

static uint32_t generate_volume_id(void) {
    return (uint32_t)time(NULL) ^ 0x5A5AA5A5;
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-F 12|16|32] [-n volume_name] [-s sec_per_clus] <image_file>\n", prog);
}

int main(int argc, char **argv) {
    int fat_type;
    const char *vol_name;
    const char *image_path;
    int sec_per_clus_arg;
    int i;
    FILE *fp;
    long file_size;
    uint32_t total_sectors;
    uint8_t sec_per_clus;
    uint16_t rsvd_sec_cnt;
    uint16_t root_ent_cnt;
    uint32_t root_dir_sectors;
    uint32_t fat_size;
    uint32_t total_clusters;
    uint32_t vol_id;
    uint8_t boot_sec[SECTOR_SIZE];
    uint8_t *sector_buf;

    fat_type = 0; /* Auto / default */
    vol_name = NULL;
    image_path = NULL;
    sec_per_clus_arg = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-F") == 0 && i + 1 < argc) {
            fat_type = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            vol_name = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            sec_per_clus_arg = atoi(argv[++i]);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            image_path = argv[i];
        }
    }

    if (!image_path) {
        print_usage(argv[0]);
        return 1;
    }

    fp = fopen(image_path, "r+b");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(fp);
        return 1;
    }

    file_size = ftell(fp);
    if (file_size < 512 * 1024) {
        fprintf(stderr, "Error: Image file too small (%ld bytes, minimum 512KB)\n", file_size);
        fclose(fp);
        return 1;
    }

    total_sectors = (uint32_t)(file_size / SECTOR_SIZE);

    /* Determine FAT type if not specified */
    if (fat_type == 0) {
        if (file_size < 4 * 1024 * 1024) {
            fat_type = 12;
        } else if (file_size < 512 * 1024 * 1024) {
            fat_type = 16;
        } else {
            fat_type = 32;
        }
    }

    if (fat_type != 12 && fat_type != 16 && fat_type != 32) {
        fprintf(stderr, "Error: Unsupported FAT type %d (must be 12, 16, or 32)\n", fat_type);
        fclose(fp);
        return 1;
    }

    /* Choose sectors per cluster */
    if (sec_per_clus_arg > 0) {
        sec_per_clus = (uint8_t)sec_per_clus_arg;
    } else {
        if (fat_type == 12) {
            sec_per_clus = 1;
        } else if (fat_type == 16) {
            if (file_size <= 16 * 1024 * 1024) sec_per_clus = 2;       /* 1KB cluster */
            else if (file_size <= 32 * 1024 * 1024) sec_per_clus = 4;  /* 2KB cluster */
            else if (file_size <= 64 * 1024 * 1024) sec_per_clus = 4;  /* 2KB cluster */
            else if (file_size <= 128 * 1024 * 1024) sec_per_clus = 8; /* 4KB cluster */
            else sec_per_clus = 16;                                    /* 8KB cluster */
        } else {
            /* FAT32 */
            if (file_size <= 64 * 1024 * 1024) sec_per_clus = 1;       /* 512B cluster */
            else if (file_size <= 256 * 1024 * 1024) sec_per_clus = 2; /* 1KB cluster */
            else if (file_size <= 1024 * 1024 * 1024) sec_per_clus = 4;/* 2KB cluster */
            else sec_per_clus = 8;                                     /* 4KB cluster */
        }
    }

    vol_id = generate_volume_id();

    sector_buf = (uint8_t*)calloc(1, SECTOR_SIZE);
    if (!sector_buf) {
        perror("calloc");
        fclose(fp);
        return 1;
    }

    if (fat_type == 12 || fat_type == 16) {
        uint32_t data_sec;
        rsvd_sec_cnt = 4;
        root_ent_cnt = 512;
        root_dir_sectors = (root_ent_cnt * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE;

        if (total_sectors < 65536) {
            /* tot_sec_16 */
        } else {
            /* tot_sec_32 */
        }

        /* Calculate FAT size */
        data_sec = total_sectors - (rsvd_sec_cnt + root_dir_sectors);
        total_clusters = data_sec / sec_per_clus;

        if (fat_type == 12) {
            fat_size = ((total_clusters + 2) * 3 / 2 + SECTOR_SIZE - 1) / SECTOR_SIZE;
        } else {
            fat_size = ((total_clusters + 2) * 2 + SECTOR_SIZE - 1) / SECTOR_SIZE;
        }

        make_bpb(boot_sec, fat_type, total_sectors, sec_per_clus, rsvd_sec_cnt,
                 root_ent_cnt, fat_size, vol_id, vol_name);

        /* Write BPB to Sector 0 */
        fseek(fp, 0, SEEK_SET);
        fwrite(boot_sec, 1, SECTOR_SIZE, fp);

        /* Zero out remaining reserved sectors */
        memset(sector_buf, 0, SECTOR_SIZE);
        for (i = 1; i < rsvd_sec_cnt; i++) {
            fwrite(sector_buf, 1, SECTOR_SIZE, fp);
        }

        /* Write FAT1 */
        memset(sector_buf, 0, SECTOR_SIZE);
        if (fat_type == 12) {
            sector_buf[0] = 0xF8;
            sector_buf[1] = 0xFF;
            sector_buf[2] = 0xFF;
        } else {
            sector_buf[0] = 0xF8;
            sector_buf[1] = 0xFF;
            sector_buf[2] = 0xFF;
            sector_buf[3] = 0xFF;
        }
        fwrite(sector_buf, 1, SECTOR_SIZE, fp);

        memset(sector_buf, 0, SECTOR_SIZE);
        for (i = 1; i < (int)fat_size; i++) {
            fwrite(sector_buf, 1, SECTOR_SIZE, fp);
        }

        /* Write FAT2 */
        if (fat_type == 12) {
            sector_buf[0] = 0xF8;
            sector_buf[1] = 0xFF;
            sector_buf[2] = 0xFF;
        } else {
            sector_buf[0] = 0xF8;
            sector_buf[1] = 0xFF;
            sector_buf[2] = 0xFF;
            sector_buf[3] = 0xFF;
        }
        fwrite(sector_buf, 1, SECTOR_SIZE, fp);

        memset(sector_buf, 0, SECTOR_SIZE);
        for (i = 1; i < (int)fat_size; i++) {
            fwrite(sector_buf, 1, SECTOR_SIZE, fp);
        }

        /* Initialize Root Directory (with volume label entry if given) */
        if (vol_name && vol_name[0]) {
            struct fat_dir_entry *label_ent = (struct fat_dir_entry*)sector_buf;
            memset(sector_buf, 0, SECTOR_SIZE);
            format_volume_label(vol_name, label_ent->name);
            label_ent->attr = 0x08; /* Volume Label */
            fwrite(sector_buf, 1, SECTOR_SIZE, fp);
            memset(sector_buf, 0, SECTOR_SIZE);
            for (i = 1; i < (int)root_dir_sectors; i++) {
                fwrite(sector_buf, 1, SECTOR_SIZE, fp);
            }
        } else {
            memset(sector_buf, 0, SECTOR_SIZE);
            for (i = 0; i < (int)root_dir_sectors; i++) {
                fwrite(sector_buf, 1, SECTOR_SIZE, fp);
            }
        }
    } else {
        /* FAT32 */
        struct fat32_fsinfo fsinfo;
        uint32_t data_sec;

        rsvd_sec_cnt = 32;
        root_ent_cnt = 0;
        root_dir_sectors = 0;

        data_sec = total_sectors - rsvd_sec_cnt;
        total_clusters = data_sec / sec_per_clus;
        fat_size = ((total_clusters + 2) * 4 + SECTOR_SIZE - 1) / SECTOR_SIZE;

        make_bpb(boot_sec, fat_type, total_sectors, sec_per_clus, rsvd_sec_cnt,
                 root_ent_cnt, fat_size, vol_id, vol_name);

        /* Write BPB to Sector 0 */
        fseek(fp, 0, SEEK_SET);
        fwrite(boot_sec, 1, SECTOR_SIZE, fp);

        /* Write FSInfo Sector (Sector 1) */
        memset(&fsinfo, 0, sizeof(fsinfo));
        fsinfo.lead_sig = 0x41615252;
        fsinfo.struc_sig = 0x61417272;
        fsinfo.free_count = total_clusters - 1; /* minus root dir cluster */
        fsinfo.nxt_free = 3;
        fsinfo.trail_sig = 0xAA550000;
        fseek(fp, 1 * SECTOR_SIZE, SEEK_SET);
        fwrite(&fsinfo, 1, sizeof(fsinfo), fp);

        /* Write Backup BPB to Sector 6 */
        fseek(fp, 6 * SECTOR_SIZE, SEEK_SET);
        fwrite(boot_sec, 1, SECTOR_SIZE, fp);

        /* Write Backup FSInfo to Sector 7 */
        fseek(fp, 7 * SECTOR_SIZE, SEEK_SET);
        fwrite(&fsinfo, 1, sizeof(fsinfo), fp);

        /* Write FAT1 (starting at rsvd_sec_cnt) */
        fseek(fp, (long)rsvd_sec_cnt * SECTOR_SIZE, SEEK_SET);
        memset(sector_buf, 0, SECTOR_SIZE);
        sector_buf[0] = 0xF8;
        sector_buf[1] = 0xFF;
        sector_buf[2] = 0xFF;
        sector_buf[3] = 0x0F; /* Cluster 0: Media */
        sector_buf[4] = 0xFF;
        sector_buf[5] = 0xFF;
        sector_buf[6] = 0xFF;
        sector_buf[7] = 0x0F; /* Cluster 1: EOC */
        sector_buf[8] = 0xFF;
        sector_buf[9] = 0xFF;
        sector_buf[10] = 0xFF;
        sector_buf[11] = 0x0F; /* Cluster 2 (Root Dir): EOC */
        fwrite(sector_buf, 1, SECTOR_SIZE, fp);

        memset(sector_buf, 0, SECTOR_SIZE);
        for (i = 1; i < (int)fat_size; i++) {
            fwrite(sector_buf, 1, SECTOR_SIZE, fp);
        }

        /* Write FAT2 */
        sector_buf[0] = 0xF8;
        sector_buf[1] = 0xFF;
        sector_buf[2] = 0xFF;
        sector_buf[3] = 0x0F;
        sector_buf[4] = 0xFF;
        sector_buf[5] = 0xFF;
        sector_buf[6] = 0xFF;
        sector_buf[7] = 0x0F;
        sector_buf[8] = 0xFF;
        sector_buf[9] = 0xFF;
        sector_buf[10] = 0xFF;
        sector_buf[11] = 0x0F;
        fwrite(sector_buf, 1, SECTOR_SIZE, fp);

        memset(sector_buf, 0, SECTOR_SIZE);
        for (i = 1; i < (int)fat_size; i++) {
            fwrite(sector_buf, 1, SECTOR_SIZE, fp);
        }

        /* Initialize Root Directory Cluster (Cluster 2) */
        fseek(fp, (long)(rsvd_sec_cnt + 2 * fat_size) * SECTOR_SIZE, SEEK_SET);
        if (vol_name && vol_name[0]) {
            struct fat_dir_entry *label_ent = (struct fat_dir_entry*)sector_buf;
            memset(sector_buf, 0, SECTOR_SIZE);
            format_volume_label(vol_name, label_ent->name);
            label_ent->attr = 0x08; /* Volume Label */
            fwrite(sector_buf, 1, SECTOR_SIZE, fp);
            memset(sector_buf, 0, SECTOR_SIZE);
            for (i = 1; i < (int)sec_per_clus; i++) {
                fwrite(sector_buf, 1, SECTOR_SIZE, fp);
            }
        } else {
            memset(sector_buf, 0, SECTOR_SIZE);
            for (i = 0; i < (int)sec_per_clus; i++) {
                fwrite(sector_buf, 1, SECTOR_SIZE, fp);
            }
        }
    }

    free(sector_buf);
    fclose(fp);

    printf("Formatted %s as FAT%d (Total Sectors: %u, Clusters: %u, Sec/Clus: %u)\n",
           image_path, fat_type, total_sectors, total_clusters, (unsigned int)sec_per_clus);

    return 0;
}
