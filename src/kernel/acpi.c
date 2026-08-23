/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "acpi.h"
#include "io.h"
#include "vga.h"
#include "pit.h"
#include "string.h"

static bool g_acpi_supported = false;
static bool g_acpi_initialized = false;
static struct acpi_fadt *g_fadt = NULL;
static uint16_t g_slp_typa = 0x2000; /* Fallback */
static uint16_t g_slp_typb = 0x2000;

static bool validate_checksum(const uint8_t *ptr, size_t length) {
    uint8_t sum;
    size_t i;

    sum = 0;
    for (i = 0; i < length; i++) {
        sum += ptr[i];
    }
    return (sum == 0);
}

static struct rsdp_descriptor *find_rsdp(void) {
    uintptr_t addr;
    uint16_t ebda_seg;
    uintptr_t ebda_addr;

    /* 1. Search in EBDA (first 1KB) */
    ebda_seg = *((const uint16_t*)0x40E);
    ebda_addr = (uintptr_t)ebda_seg << 4;
    if (ebda_addr >= 0x80000 && ebda_addr < 0xA0000) {
        for (addr = ebda_addr; addr < ebda_addr + 1024; addr += 16) {
            if (memcmp((const void*)addr, "RSD PTR ", 8) == 0) {
                if (validate_checksum((const uint8_t*)addr, 20)) {
                    return (struct rsdp_descriptor*)addr;
                }
            }
        }
    }

    /* 2. Search in BIOS ROM space (0x000E0000 to 0x000FFFFF) */
    for (addr = 0x000E0000; addr < 0x00100000; addr += 16) {
        if (memcmp((const void*)addr, "RSD PTR ", 8) == 0) {
            if (validate_checksum((const uint8_t*)addr, 20)) {
                return (struct rsdp_descriptor*)addr;
            }
        }
    }

    return NULL;
}

static struct acpi_fadt *find_fadt(struct acpi_sdt_header *rsdt) {
    size_t entries;
    size_t i;
    const uint32_t *table_ptrs;

    if (!rsdt || !validate_checksum((const uint8_t*)rsdt, rsdt->length)) {
        return NULL;
    }

    entries = (rsdt->length - sizeof(struct acpi_sdt_header)) / 4;
    table_ptrs = (const uint32_t*)(const void*)((const uint8_t*)rsdt + sizeof(struct acpi_sdt_header));

    for (i = 0; i < entries; i++) {
        struct acpi_sdt_header *header;
        header = (struct acpi_sdt_header*)(uintptr_t)table_ptrs[i];
        if (header && memcmp(header->signature, "FACP", 4) == 0) {
            if (validate_checksum((const uint8_t*)header, header->length)) {
                return (struct acpi_fadt*)header;
            }
        }
    }

    return NULL;
}

static uint16_t parse_aml_integer(const uint8_t **stream, const uint8_t *end) {
    const uint8_t *p;
    uint16_t val;

    p = *stream;
    val = 0;

    if (p >= end) return 0;

    if (*p == 0x00) { /* ZeroOp */
        val = 0;
        p++;
    } else if (*p == 0x01) { /* OneOp */
        val = 1;
        p++;
    } else if (*p == 0xFF) { /* OnesOp */
        val = 0xFFFF;
        p++;
    } else if (*p == 0x0A) { /* BytePrefix */
        p++;
        if (p < end) {
            val = *p++;
        }
    } else if (*p == 0x0B) { /* WordPrefix */
        p++;
        if (p + 1 < end) {
            val = p[0] | (p[1] << 8);
            p += 2;
        }
    } else if (*p == 0x0C) { /* DWordPrefix */
        p++;
        if (p + 3 < end) {
            val = p[0] | (p[1] << 8);
            p += 4;
        }
    } else {
        val = *p++;
    }

    *stream = p;
    return val;
}

static bool parse_s5_package(const uint8_t *dsdt_bytes, size_t dsdt_len, uint16_t *slp_typa, uint16_t *slp_typb) {
    size_t i;
    const uint8_t *end;

    end = dsdt_bytes + dsdt_len;

    for (i = 0; i + 4 < dsdt_len; i++) {
        if (dsdt_bytes[i] == '_' && dsdt_bytes[i+1] == 'S' &&
            dsdt_bytes[i+2] == '5' && dsdt_bytes[i+3] == '_') {
            const uint8_t *p;

            p = dsdt_bytes + i + 4;
            if (p >= end) break;

            /* Check for PackageOp (0x12) */
            if (*p == 0x12) {
                uint8_t pkg_lead;
                uint8_t byte_count;

                p++;
                if (p >= end) break;

                /* Parse PkgLength */
                pkg_lead = *p++;
                byte_count = (pkg_lead >> 6) & 0x03;
                p += byte_count; /* Skip remaining package length bytes */

                if (p >= end) break;

                /* Skip NumElements */
                p++;
                if (p >= end) break;

                /* Parse SLP_TYPa */
                *slp_typa = parse_aml_integer(&p, end);

                /* Parse SLP_TYPb */
                *slp_typb = parse_aml_integer(&p, end);

                return true;
            }
        }
    }

    return false;
}

bool acpi_init(void) {
    struct rsdp_descriptor *rsdp;
    struct acpi_sdt_header *rsdt;
    struct acpi_sdt_header *dsdt_hdr;

    char oem_str[7];

    if (g_acpi_initialized) return g_acpi_supported;
    g_acpi_initialized = true;

    rsdp = find_rsdp();
    if (!rsdp) {
        kprintf("[ACPI] RSDP not found in memory.\n");
        return false;
    }

    memcpy(oem_str, rsdp->oem_id, 6);
    oem_str[6] = '\0';
    kprintf("[ACPI] Found RSDP at %p (OEM: %s, Rev %u)\n",
            (void*)rsdp, oem_str, (uint32_t)rsdp->revision);

    rsdt = (struct acpi_sdt_header*)(uintptr_t)rsdp->rsdt_address;
    if (!rsdt) {
        kprintf("[ACPI] RSDT table pointer is NULL.\n");
        return false;
    }

    g_fadt = find_fadt(rsdt);
    if (!g_fadt) {
        kprintf("[ACPI] FADT table not found in RSDT.\n");
        return false;
    }

    kprintf("[ACPI] Found FADT at %p (PM1a_CNT=0x%x, PM1b_CNT=0x%x, SMI_CMD=0x%x)\n",
            (void*)g_fadt, g_fadt->pm1a_cnt_blk, g_fadt->pm1b_cnt_blk, g_fadt->smi_cmd);

    /* Parse DSDT for _S5 sleep type */
    dsdt_hdr = (struct acpi_sdt_header*)(uintptr_t)g_fadt->dsdt;
    if (dsdt_hdr && validate_checksum((const uint8_t*)dsdt_hdr, dsdt_hdr->length)) {
        uint16_t typa;
        uint16_t typb;

        typa = 0;
        typb = 0;
        if (parse_s5_package((const uint8_t*)dsdt_hdr + sizeof(struct acpi_sdt_header),
                             dsdt_hdr->length - sizeof(struct acpi_sdt_header),
                             &typa, &typb)) {
            g_slp_typa = typa;
            g_slp_typb = typb;
            kprintf("[ACPI] Extracted S5 package: SLP_TYPa=0x%x, SLP_TYPb=0x%x\n",
                    g_slp_typa, g_slp_typb);
        } else {
            /* Default fallback for S5 (type 0 on QEMU/Bochs) */
            g_slp_typa = 0x00;
            g_slp_typb = 0x00;
            kprintf("[ACPI] _S5 not found in DSDT, using default S5 sleep types.\n");
        }
    } else {
        g_slp_typa = 0x00;
        g_slp_typb = 0x00;
    }

    g_acpi_supported = true;
    return true;
}

bool acpi_is_supported(void) {
    if (!g_acpi_initialized) {
        acpi_init();
    }
    return g_acpi_supported;
}

void acpi_poweroff(void) {
    if (!g_acpi_initialized) {
        acpi_init();
    }

    if (g_acpi_supported && g_fadt != NULL) {
        int timeout;

        kprintf("[ACPI] Initiating ACPI S5 Soft Off...\n");

        /* 1. Enable ACPI Mode if SMI_CMD is defined and not yet enabled */
        if (g_fadt->smi_cmd != 0 && g_fadt->acpi_enable != 0) {
            uint16_t pm1_cnt;
            pm1_cnt = inw((uint16_t)g_fadt->pm1a_cnt_blk);
            if ((pm1_cnt & 1) == 0) { /* SCI_EN bit 0 is not set */
                outb((uint16_t)g_fadt->smi_cmd, g_fadt->acpi_enable);
                timeout = 1000;
                while (timeout-- > 0) {
                    if (inw((uint16_t)g_fadt->pm1a_cnt_blk) & 1) {
                        break;
                    }
                    pit_delay_ms(1);
                }
            }
        }

        /* 2. Write to PM1a_CNT_BLK and PM1b_CNT_BLK */
        if (g_fadt->pm1a_cnt_blk != 0) {
            uint16_t val_a;
            val_a = (g_slp_typa << 10) | (1 << 13); /* SLP_EN bit 13 */
            outw((uint16_t)g_fadt->pm1a_cnt_blk, val_a);
        }

        if (g_fadt->pm1b_cnt_blk != 0) {
            uint16_t val_b;
            val_b = (g_slp_typb << 10) | (1 << 13);
            outw((uint16_t)g_fadt->pm1b_cnt_blk, val_b);
        }

        pit_delay_ms(100);
    }

    /* Fallback to standard hardware shutdown ports & CPU halt */
    arch_shutdown();
}
