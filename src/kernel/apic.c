/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "apic.h"
#include "acpi.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "io.h"
#include "vga.h"

#define LAPIC_ID            0x020
#define LAPIC_VERSION       0x030
#define LAPIC_TPR           0x080
#define LAPIC_EOI           0x0B0
#define LAPIC_LDR           0x0D0
#define LAPIC_DFR           0x0E0
#define LAPIC_SVR           0x0F0
#define LAPIC_ESR           0x280
#define LAPIC_LVT_TIMER     0x320
#define LAPIC_LVT_THERMAL   0x330
#define LAPIC_LVT_PERF      0x340
#define LAPIC_LVT_LINT0     0x350
#define LAPIC_LVT_LINT1     0x360
#define LAPIC_LVT_ERROR     0x370
#define LAPIC_TIMER_INITCNT 0x380
#define LAPIC_TIMER_CURRCNT 0x390
#define LAPIC_TIMER_DIV     0x3E0

#define APIC_DISABLE        0x10000
#define APIC_TIMER_PERIODIC 0x20000

static bool g_apic_active = false;
static uintptr_t g_lapic_base = 0xFEE00000;
static uint32_t g_ticks_per_ms = 0;

static uint32_t lapic_read(uint32_t reg) {
    return mmio_read32(g_lapic_base + reg);
}

static void lapic_write(uint32_t reg, uint32_t val) {
    mmio_write32(g_lapic_base + reg, val);
}

void apic_send_eoi(void) {
    if (g_apic_active) {
        lapic_write(LAPIC_EOI, 0);
    }
}

bool apic_is_active(void) {
    return g_apic_active;
}

uintptr_t apic_get_base(void) {
    return g_lapic_base;
}

bool apic_init(void) {
    uint32_t msr_hi;
    uint32_t msr_lo;
    uintptr_t acpi_lapic;
    uint32_t ticks_in_10ms;
    uint32_t curr_cnt;

    /* 1. Verify CPU APIC support via CPUID */
    if (!cpu_has_apic()) {
        kprintf("[APIC] CPU does not support on-chip APIC.\n");
        return false;
    }

    /* 2. Verify ACPI MADT / LAPIC address */
    acpi_lapic = acpi_get_lapic_address();
    if (acpi_lapic != 0) {
        g_lapic_base = acpi_lapic;
    } else {
        kprintf("[APIC] ACPI MADT table not found. Using default LAPIC base %p\n", (void*)g_lapic_base);
    }

    /* 3. Enable Local APIC in MSR (IA32_APIC_BASE = 0x1B) */
    msr_hi = 0;
    msr_lo = cpu_get_msr(0x1B, &msr_hi);
    if ((msr_lo & (1 << 11)) == 0) {
        /* Enable APIC Global bit 11 */
        msr_lo |= (1 << 11);
        cpu_set_msr(0x1B, msr_lo, msr_hi);
    }

    /* Update base from MSR if valid */
    if ((msr_lo & 0xFFFFF000) != 0) {
        g_lapic_base = (uintptr_t)(msr_lo & 0xFFFFF000);
    }

    kprintf("[APIC] Initializing Local APIC at %p...\n", (void*)g_lapic_base);

    /* 4. Reset & Configure Local APIC registers */
    lapic_write(LAPIC_DFR, 0xFFFFFFFF); /* Flat model */
    lapic_write(LAPIC_LDR, (lapic_read(LAPIC_ID) & 0xFF000000));
    lapic_write(LAPIC_TPR, 0x00);        /* Clear Task Priority (accept all interrupts) */
    lapic_write(LAPIC_LVT_LINT0, APIC_DISABLE);
    lapic_write(LAPIC_LVT_LINT1, APIC_DISABLE);
    lapic_write(LAPIC_LVT_ERROR, APIC_DISABLE);
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_EOI, 0);

    /* Enable APIC via Spurious Interrupt Vector Register (Vector 0xFF + Bit 8 APIC Enable) */
    lapic_write(LAPIC_SVR, 0x1FF);

    /* 5. Calibrate APIC Timer using 10 ms delay */
    /* Set Timer Divide Configuration to 16 (0x03) */
    lapic_write(LAPIC_TIMER_DIV, 0x03);

    /* Set Timer to Masked One-Shot Mode */
    lapic_write(LAPIC_LVT_TIMER, APIC_DISABLE);
    lapic_write(LAPIC_TIMER_INITCNT, 0xFFFFFFFF);

    /* Delay exactly 10 ms using PIT delay */
    pit_delay_ms(10);

    curr_cnt = lapic_read(LAPIC_TIMER_CURRCNT);
    ticks_in_10ms = 0xFFFFFFFF - curr_cnt;
    g_ticks_per_ms = ticks_in_10ms / 10;

    if (g_ticks_per_ms < 1000) {
        /* Fallback calibration if timer did not tick or underflowed */
        g_ticks_per_ms = 100000;
    }

    kprintf("[APIC] Calibrated APIC Timer: %u ticks/ms (%u ticks in 10ms)\n",
            g_ticks_per_ms, ticks_in_10ms);

    /* 6. Mask IRQ0 on legacy 8259 PIC so PIT timer stops interrupting */
    pic_mask_irq(0);

    /* 7. Start APIC Timer in Periodic Mode on Vector 32 (1000 Hz) */
    lapic_write(LAPIC_TIMER_DIV, 0x03);
    lapic_write(LAPIC_LVT_TIMER, APIC_TIMER_PERIODIC | APIC_TIMER_VECTOR);
    lapic_write(LAPIC_TIMER_INITCNT, g_ticks_per_ms);

    g_apic_active = true;
    return true;
}
