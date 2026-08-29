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

/* ACPI Generic Address Structure (GAS) */
struct acpi_gas {
    uint8_t address_space_id; /* 0=System Memory, 1=System I/O, 2=PCI Config */
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t access_size;      /* 1=Byte, 2=Word, 3=Dword, 4=Qword */
    uint32_t address_lo;
    uint32_t address_hi;
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
    struct acpi_gas reset_reg;
    uint8_t reset_value;
    uint8_t arm_boot_arch;
    uint8_t fadt_minor_version;
    uint32_t x_firmware_ctrl_lo;
    uint32_t x_firmware_ctrl_hi;
    uint32_t x_dsdt_lo;
    uint32_t x_dsdt_hi;
    struct acpi_gas x_pm1a_evt_blk;
    struct acpi_gas x_pm1b_evt_blk;
    struct acpi_gas x_pm1a_cnt_blk;
    struct acpi_gas x_pm1b_cnt_blk;
    struct acpi_gas x_pm2_cnt_blk;
    struct acpi_gas x_pm_tmr_blk;
    struct acpi_gas x_gpe0_blk;
    struct acpi_gas x_gpe1_blk;
} PACKED;

/* MADT Table (Multiple APIC Description Table) */
struct acpi_madt {
    struct acpi_sdt_header header;
    uint32_t local_apic_address;
    uint32_t flags;
} PACKED;

/* MADT Entry Header */
struct acpi_madt_entry_header {
    uint8_t type;
    uint8_t length;
} PACKED;

/* Type 0: Processor Local APIC */
struct acpi_madt_local_apic {
    struct acpi_madt_entry_header header;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;
} PACKED;

/* Type 1: I/O APIC */
struct acpi_madt_io_apic {
    struct acpi_madt_entry_header header;
    uint8_t io_apic_id;
    uint8_t reserved;
    uint32_t io_apic_address;
    uint32_t global_system_interrupt_base;
} PACKED;

/* Type 2: Interrupt Source Override */
struct acpi_madt_interrupt_override {
    struct acpi_madt_entry_header header;
    uint8_t bus;
    uint8_t source;
    uint32_t global_system_interrupt;
    uint16_t flags;
} PACKED;

/* Type 5: Local APIC Address Override */
struct acpi_madt_local_apic_override {
    struct acpi_madt_entry_header header;
    uint16_t reserved;
    uint32_t local_apic_address_low;
    uint32_t local_apic_address_high;
} PACKED;

/* ACPI Public API */
struct multiboot_info;
bool acpi_init(struct multiboot_info *mbi);
void acpi_poweroff(void);
void acpi_reboot(void);
bool acpi_is_supported(void);
struct acpi_sdt_header *acpi_find_table(const char *signature);
struct acpi_madt *acpi_get_madt(void);
uintptr_t acpi_get_lapic_address(void);
struct acpi_fadt *acpi_get_fadt(void);
uint16_t acpi_get_pm_timer_port(void);
bool acpi_pm_timer_is_32bit(void);

#endif /* GEMIOS_ACPI_H */
