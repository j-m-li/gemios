/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "acpi.h"
#include "pci.h"
#include "io.h"
#include "vga.h"
#include "timer.h"
#include "string.h"

#include "multiboot.h"

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

static struct rsdp_descriptor *find_rsdp(struct multiboot_info *mbi) {
    uintptr_t addr;
    uint16_t ebda_seg;
    uintptr_t ebda_addr;

    /* 1. Search in Multiboot config_table (e.g. from UEFI) */
    if (mbi && (mbi->flags & MULTIBOOT_INFO_CONFIG_TABLE) && mbi->config_table != 0) {
        if (memcmp((const void*)(uintptr_t)mbi->config_table, "RSD PTR ", 8) == 0) {
            if (validate_checksum((const uint8_t*)(uintptr_t)mbi->config_table, 20)) {
                return (struct rsdp_descriptor*)(uintptr_t)mbi->config_table;
            }
        }
    }

    /* 2. Search in EBDA (first 1KB) */
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

    /* 3. Search in BIOS ROM space (0x000E0000 to 0x000FFFFF) */
    for (addr = 0x000E0000; addr < 0x00100000; addr += 16) {
        if (memcmp((const void*)addr, "RSD PTR ", 8) == 0) {
            if (validate_checksum((const uint8_t*)addr, 20)) {
                return (struct rsdp_descriptor*)addr;
            }
        }
    }

    return NULL;
}

static struct acpi_sdt_header *g_rsdt = NULL;
static struct acpi_madt *g_madt = NULL;
static uintptr_t g_lapic_addr = 0;

struct acpi_sdt_header *acpi_find_table(const char *signature) {
    size_t entries;
    size_t i;
    const uint32_t *table_ptrs;

    if (!g_rsdt || !validate_checksum((const uint8_t*)g_rsdt, g_rsdt->length)) {
        return NULL;
    }

    entries = (g_rsdt->length - sizeof(struct acpi_sdt_header)) / 4;
    table_ptrs = (const uint32_t*)(const void*)((const uint8_t*)g_rsdt + sizeof(struct acpi_sdt_header));

    for (i = 0; i < entries; i++) {
        struct acpi_sdt_header *header;
        header = (struct acpi_sdt_header*)(uintptr_t)table_ptrs[i];
        if (header && memcmp(header->signature, signature, 4) == 0) {
            if (validate_checksum((const uint8_t*)header, header->length)) {
                return header;
            }
        }
    }

    return NULL;
}

struct acpi_madt *acpi_get_madt(void) {
    return g_madt;
}

uintptr_t acpi_get_lapic_address(void) {
    return g_lapic_addr;
}

struct acpi_fadt *acpi_get_fadt(void) {
    return g_fadt;
}

uint16_t acpi_get_pm_timer_port(void) {
    if (!g_fadt) return 0;
    if (g_fadt->pm_tmr_blk != 0) {
        return (uint16_t)g_fadt->pm_tmr_blk;
    }
    if (g_fadt->header.length >= sizeof(struct acpi_fadt)) {
        if (g_fadt->x_pm_tmr_blk.address_space_id == 1 && g_fadt->x_pm_tmr_blk.address_lo != 0) {
            return (uint16_t)g_fadt->x_pm_tmr_blk.address_lo;
        }
    }
    return 0;
}

bool acpi_pm_timer_is_32bit(void) {
    if (!g_fadt) return false;
    return (g_fadt->flags & (1 << 8)) != 0;
}

static struct acpi_fadt *find_fadt(struct acpi_sdt_header *rsdt) {
    UNUSED(rsdt);
    return (struct acpi_fadt*)acpi_find_table("FACP");
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

static struct multiboot_info *g_mbi_saved = NULL;

bool acpi_init(struct multiboot_info *mbi) {
    struct rsdp_descriptor *rsdp;
    struct acpi_sdt_header *rsdt;
    struct acpi_sdt_header *dsdt_hdr;

    char oem_str[7];

    if (mbi) g_mbi_saved = mbi;
    if (g_acpi_initialized) return g_acpi_supported;
    g_acpi_initialized = true;

    rsdp = find_rsdp(mbi ? mbi : g_mbi_saved);
    if (!rsdp) {
        kprintf("[ACPI] RSDP not found in memory.\n");
        return false;
    }

    memcpy(oem_str, rsdp->oem_id, 6);
    oem_str[6] = '\0';
    kprintf("[ACPI] Found RSDP at %p (OEM: %s, Rev %u)\n",
            (void*)rsdp, oem_str, (uint32_t)rsdp->revision);

    g_rsdt = (struct acpi_sdt_header*)(uintptr_t)rsdp->rsdt_address;
    if (!g_rsdt) {
        kprintf("[ACPI] RSDT table pointer is NULL.\n");
        return false;
    }

    /* Parse MADT (Multiple APIC Description Table) */
    g_madt = (struct acpi_madt*)acpi_find_table("APIC");
    if (g_madt) {
        const uint8_t *ptr;
        const uint8_t *end;
        uint32_t cpu_count = 0;
        uint32_t ioapic_count = 0;

        g_lapic_addr = (uintptr_t)g_madt->local_apic_address;

        ptr = (const uint8_t*)g_madt + sizeof(struct acpi_madt);
        end = (const uint8_t*)g_madt + g_madt->header.length;

        while (ptr + sizeof(struct acpi_madt_entry_header) <= end) {
            const struct acpi_madt_entry_header *entry = (const struct acpi_madt_entry_header*)ptr;
            if (entry->length == 0) break;

            if (entry->type == 0) { /* Local APIC */
                const struct acpi_madt_local_apic *lapic = (const struct acpi_madt_local_apic*)ptr;
                if (lapic->flags & 1) {
                    cpu_count++;
                }
            } else if (entry->type == 1) { /* I/O APIC */
                ioapic_count++;
            } else if (entry->type == 5) { /* 64-bit Local APIC Address Override */
                const struct acpi_madt_local_apic_override *ovr = (const struct acpi_madt_local_apic_override*)ptr;
                g_lapic_addr = (uintptr_t)ovr->local_apic_address_low;
            }

            ptr += entry->length;
        }

        kprintf("[ACPI] Found MADT (APIC) Table: LAPIC Base: %p (%u CPU(s), %u I/O APIC(s))\n",
                (void*)g_lapic_addr, cpu_count, ioapic_count);
    } else {
        kprintf("[ACPI] MADT (APIC) Table not found in RSDT.\n");
    }

    g_fadt = find_fadt(g_rsdt);
    if (!g_fadt) {
        kprintf("[ACPI] FADT table not found in RSDT.\n");
    } else {
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
                g_slp_typa = 0x00;
                g_slp_typb = 0x00;
            }
        }
    }

    g_acpi_supported = true;
    return true;
}

bool acpi_is_supported(void) {
    if (!g_acpi_initialized) {
        acpi_init(NULL);
    }
    return g_acpi_supported;
}

void acpi_poweroff(void) {
    if (!g_acpi_initialized) {
        acpi_init(NULL);
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
                    timer_delay_ms(1);
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

        timer_delay_ms(100);
    }

    /* Fallback to standard hardware shutdown ports & CPU halt */
    arch_shutdown();
}

void acpi_reboot(void) {
    if (!g_acpi_initialized) {
        acpi_init(NULL);
    }

    kprintf("[ACPI] Initiating System Reboot...\n");

    /* 1. ACPI FADT Reset Register (ACPI 2.0+ standard) */
    if (g_acpi_supported && g_fadt != NULL) {
        if (g_fadt->header.length >= 129 && (g_fadt->flags & (1 << 10))) {
            struct acpi_gas *reset = &g_fadt->reset_reg;
            uint8_t val = g_fadt->reset_value;

            kprintf("[ACPI] Using FADT Reset Register (Space=%u, Addr=0x%x, Val=0x%02x)\n",
                    reset->address_space_id, reset->address_lo, val);

            if (reset->address_space_id == 1) {
                /* System I/O Port */
                uint16_t port = (uint16_t)reset->address_lo;
                if (reset->register_bit_width == 16 || reset->access_size == 2) {
                    outw(port, (uint16_t)val);
                } else if (reset->register_bit_width == 32 || reset->access_size == 3) {
                    outl(port, (uint32_t)val);
                } else {
                    outb(port, val);
                }
                timer_delay_ms(50);
            } else if (reset->address_space_id == 0) {
                /* System Memory / MMIO */
                mmio_write8((uintptr_t)reset->address_lo, val);
                timer_delay_ms(50);
            } else if (reset->address_space_id == 2) {
                /* PCI Config Space:
                 * bits 0..15: Register offset
                 * bits 16..31: Function
                 * bits 32..47: Device (in address_hi)
                 * bits 48..63: Bus (in address_hi)
                 */
                uint8_t reg = (uint8_t)(reset->address_lo & 0xFF);
                uint8_t func = (uint8_t)((reset->address_lo >> 16) & 0x07);
                uint8_t dev = (uint8_t)(reset->address_hi & 0x1F);
                uint8_t bus = (uint8_t)((reset->address_hi >> 16) & 0xFF);
                pci_write_config8(bus, dev, func, reg, val);
                timer_delay_ms(50);
            }
        }
    }

    /* 2. PCI Chipset Reset Port 0xCF9 (Standard for Intel / AMD / VirtualBox / QEMU / Bare Metal) */
    outb(0xCF9, 0x02);
    timer_delay_ms(2);
    outb(0xCF9, 0x06); /* Hard reset */
    timer_delay_ms(10);
    outb(0xCF9, 0x0E); /* Full power cycle reset */
    timer_delay_ms(50);

    /* 3. 8042 Keyboard Controller Pulse Reset */
    {
        int timeout = 10000;
        while ((inb(0x64) & 0x02) && timeout-- > 0) {
            io_wait();
        }
        outb(0x64, 0xFE);
        timer_delay_ms(50);
    }

    /* 4. Fallback: CPU Triple Fault */
    arch_reboot();
}
