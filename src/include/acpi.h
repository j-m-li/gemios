/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_ACPI_H
#define GEMIOS_ACPI_H

#include "types.h"

/* RSDP Structure (ACPI 1.0 / 2.0) */
struct rsdp_descriptor {
    char signature[8];        /* "RSD PTR " */
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} PACKED;

struct rsdp_descriptor20 {
    struct rsdp_descriptor first_part;
    uint32_t length;
    uint32_t xsdt_address_low;
    uint32_t xsdt_address_high;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} PACKED;

/* Standard ACPI Table Header */
struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} PACKED;

/* FADT Table (Fixed ACPI Description Table) */
struct acpi_fadt {
    struct acpi_sdt_header header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved;
    uint8_t preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_len;
    uint8_t gpe1_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alrm;
    uint8_t mon_alrm;
    uint8_t century;
    uint16_t iapc_boot_arch;
    uint8_t reserved2;
    uint32_t flags;
} PACKED;

/* ACPI Public API */
bool acpi_init(void);
void acpi_poweroff(void);
bool acpi_is_supported(void);

#endif /* GEMIOS_ACPI_H */
