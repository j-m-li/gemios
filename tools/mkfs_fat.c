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

/* Structure of BPB for FAT12 / FAT16 / FAT32 */
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
#pragma pack(pop)

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
    struct fat_bpb bpb;
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
    memset(&bpb, 0, sizeof(bpb));

    bpb.jmp_boot[0] = 0xEB;
    bpb.jmp_boot[1] = (fat_type == 32) ? 0x58 : 0x3C;
    bpb.jmp_boot[2] = 0x90;
    memcpy(bpb.oem_name, "GEMIOS  ", 8);
    bpb.bytes_per_sec = SECTOR_SIZE;
    bpb.sec_per_clus = sec_per_clus;
    bpb.num_fats = 2;
    bpb.media = 0xF8;
    bpb.sec_per_trk = 32;
    bpb.num_heads = 64;
    bpb.hidd_sec = 0;

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

        bpb.rsvd_sec_cnt = rsvd_sec_cnt;
        bpb.root_ent_cnt = root_ent_cnt;

        if (total_sectors < 65536) {
            bpb.tot_sec_16 = (uint16_t)total_sectors;
            bpb.tot_sec_32 = 0;
        } else {
            bpb.tot_sec_16 = 0;
            bpb.tot_sec_32 = total_sectors;
        }

        /* Calculate FAT size */
        data_sec = total_sectors - (rsvd_sec_cnt + root_dir_sectors);
        total_clusters = data_sec / sec_per_clus;

        if (fat_type == 12) {
            fat_size = ((total_clusters + 2) * 3 / 2 + SECTOR_SIZE - 1) / SECTOR_SIZE;
        } else {
            fat_size = ((total_clusters + 2) * 2 + SECTOR_SIZE - 1) / SECTOR_SIZE;
        }
        bpb.fat_sz_16 = (uint16_t)fat_size;

        bpb.spec.fat16.drv_num = 0x80;
        bpb.spec.fat16.reserved1 = 0;
        bpb.spec.fat16.boot_sig = 0x29;
        bpb.spec.fat16.vol_id = vol_id;
        format_volume_label(vol_name ? vol_name : (fat_type == 12 ? "GEMIOS12" : "GEMIOS16"), bpb.spec.fat16.vol_lab);
        memcpy(bpb.spec.fat16.fil_sys_type, (fat_type == 12 ? "FAT12   " : "FAT16   "), 8);
        bpb.spec.fat16.signature = 0xAA55;

        /* Write BPB to Sector 0 */
        fseek(fp, 0, SEEK_SET);
        fwrite(&bpb, 1, sizeof(bpb), fp);

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

        bpb.rsvd_sec_cnt = rsvd_sec_cnt;
        bpb.root_ent_cnt = 0;
        bpb.tot_sec_16 = 0;
        bpb.tot_sec_32 = total_sectors;

        data_sec = total_sectors - rsvd_sec_cnt;
        total_clusters = data_sec / sec_per_clus;
        fat_size = ((total_clusters + 2) * 4 + SECTOR_SIZE - 1) / SECTOR_SIZE;

        bpb.fat_sz_16 = 0;
        bpb.spec.fat32.fat_sz_32 = fat_size;
        bpb.spec.fat32.ext_flags = 0;
        bpb.spec.fat32.fs_ver = 0;
        bpb.spec.fat32.root_clus = 2;
        bpb.spec.fat32.fs_info = 1;
        bpb.spec.fat32.bk_boot_sec = 6;
        bpb.spec.fat32.drv_num = 0x80;
        bpb.spec.fat32.reserved1 = 0;
        bpb.spec.fat32.boot_sig = 0x29;
        bpb.spec.fat32.vol_id = vol_id;
        format_volume_label(vol_name ? vol_name : "GEMIOS32", bpb.spec.fat32.vol_lab);
        memcpy(bpb.spec.fat32.fil_sys_type, "FAT32   ", 8);
        bpb.spec.fat32.signature = 0xAA55;

        /* Write BPB to Sector 0 */
        fseek(fp, 0, SEEK_SET);
        fwrite(&bpb, 1, sizeof(bpb), fp);

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
        fwrite(&bpb, 1, sizeof(bpb), fp);

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
