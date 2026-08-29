/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "timer.h"
#include "acpi.h"
#include "io.h"
#include "pit.h"
#include "vga.h"

static bool g_pm_timer_available = false;
static uint16_t g_pm_timer_port = 0;
static bool g_pm_timer_32bit = false;
static uint32_t g_pm_timer_mask = 0x00FFFFFF;

static bool g_tsc_available = false;
static uint32_t g_tsc_mhz = 0;
static uint32_t g_tsc_ticks_per_us = 0;

static void acpi_pm_timer_init(void) {
    uint32_t val1;
    uint32_t val2;
    int i;

    g_pm_timer_port = acpi_get_pm_timer_port();
    if (g_pm_timer_port == 0) {
        kprintf("[Timer] ACPI PM-Timer port not defined in FADT.\n");
        return;
    }

    g_pm_timer_32bit = acpi_pm_timer_is_32bit();
    g_pm_timer_mask = g_pm_timer_32bit ? 0xFFFFFFFF : 0x00FFFFFF;

    /* Verify that the PM timer is actually ticking */
    val1 = inl(g_pm_timer_port) & g_pm_timer_mask;
    val2 = val1;
    for (i = 0; i < 1000; i++) {
        io_wait();
        val2 = inl(g_pm_timer_port) & g_pm_timer_mask;
        if (val2 != val1) {
            g_pm_timer_available = true;
            break;
        }
    }

    if (g_pm_timer_available) {
        kprintf("[Timer] Initialized ACPI PM-Timer at port 0x%04x (%s, 3.579545 MHz)\n",
                g_pm_timer_port, g_pm_timer_32bit ? "32-bit" : "24-bit");
    } else {
        kprintf("[Timer] ACPI PM-Timer port 0x%04x not incrementing.\n", g_pm_timer_port);
    }
}

static void tsc_init(void) {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    eax = 0;
    ebx = 0;
    ecx = 0;
    edx = 0;

    if (!cpu_has_tsc()) {
        kprintf("[Timer] CPU does not support TSC.\n");
        return;
    }

    /* 1. Try CPUID Leaf 0x15 (Crystal Clock Frequency / ART) */
    cpu_cpuid(0x15, 0, &eax, &ebx, &ecx, &edx);
    if (eax != 0 && ebx != 0 && ecx != 0) {
        uint32_t art_mhz = ecx / 1000000;
        if (art_mhz > 0) {
            g_tsc_mhz = (art_mhz * ebx) / eax;
        }
    }

    /* 2. Try CPUID Leaf 0x16 (Processor Base Frequency in MHz) */
    if (g_tsc_mhz == 0) {
        cpu_cpuid(0x16, 0, &eax, &ebx, &ecx, &edx);
        if (eax != 0) {
            g_tsc_mhz = eax;
        }
    }

    /* 3. Calibrate TSC against ACPI PM-Timer over 10 ms (35,795 ticks) */
    if (g_tsc_mhz == 0 && g_pm_timer_available) {
        uint32_t tsc_start = cpu_rdtsc_lo();
        acpi_pm_timer_delay_ticks(35795); /* 10 ms on 3.579545 MHz clock */
        uint32_t tsc_end = cpu_rdtsc_lo();
        if (tsc_end > tsc_start) {
            uint32_t delta_10ms = tsc_end - tsc_start;
            g_tsc_mhz = delta_10ms / 10000;
        }
    }

    if (g_tsc_mhz > 0) {
        g_tsc_ticks_per_us = g_tsc_mhz;
        if (g_tsc_ticks_per_us == 0) g_tsc_ticks_per_us = 1;
        g_tsc_available = true;

        kprintf("[Timer] Calibrated Invariant TSC: %u MHz (%u ticks/us)\n",
                g_tsc_mhz, g_tsc_ticks_per_us);
    }
}

void timer_init(void) {
    acpi_pm_timer_init();
    tsc_init();
}

bool acpi_pm_timer_is_available(void) {
    return g_pm_timer_available;
}

bool tsc_is_available(void) {
    return g_tsc_available;
}

uint32_t acpi_pm_timer_read(void) {
    if (!g_pm_timer_available) return 0;
    return inl(g_pm_timer_port) & g_pm_timer_mask;
}

void acpi_pm_timer_delay_ticks(uint32_t ticks) {
    uint32_t start;
    uint32_t elapsed;
    uint32_t last;

    if (!g_pm_timer_available || ticks == 0) return;

    start = acpi_pm_timer_read();
    elapsed = 0;
    last = start;

    while (elapsed < ticks) {
        uint32_t now = acpi_pm_timer_read();
        if (now >= last) {
            elapsed += (now - last);
        } else {
            elapsed += ((g_pm_timer_mask + 1) + now - last);
        }
        last = now;
        cpu_pause();
    }
}

void acpi_pm_timer_delay_us(uint32_t us) {
    uint32_t ticks;
    if (us == 0) return;

    /* 3.579545 ticks per us -> exact formula without 64-bit overflow for us < 740,000 */
    while (us > 500000) {
        ticks = 500000 * 3 + (500000 * 5795) / 10000;
        acpi_pm_timer_delay_ticks(ticks);
        us -= 500000;
    }
    ticks = us * 3 + (us * 5795) / 10000;
    if (ticks == 0 && us > 0) ticks = 1;
    acpi_pm_timer_delay_ticks(ticks);
}

void acpi_pm_timer_delay_ms(uint32_t ms) {
    uint32_t ticks;
    if (ms == 0) return;

    /* 3579.545 ticks per ms */
    while (ms > 1000) {
        ticks = 3579545;
        acpi_pm_timer_delay_ticks(ticks);
        ms -= 1000;
    }
    ticks = ms * 3579 + (ms * 545) / 1000;
    if (ticks == 0 && ms > 0) ticks = 1;
    acpi_pm_timer_delay_ticks(ticks);
}

uint32_t tsc_get_mhz(void) {
    return g_tsc_mhz;
}

void tsc_delay_us(uint32_t us) {
    uint32_t start;
    uint32_t wait_ticks;

    if (!g_tsc_available || us == 0) return;

    while (us > 100000) {
        start = cpu_rdtsc_lo();
        wait_ticks = 100000 * g_tsc_ticks_per_us;
        while ((cpu_rdtsc_lo() - start) < wait_ticks) {
            cpu_pause();
        }
        us -= 100000;
    }

    start = cpu_rdtsc_lo();
    wait_ticks = us * g_tsc_ticks_per_us;
    while ((cpu_rdtsc_lo() - start) < wait_ticks) {
        cpu_pause();
    }
}

void tsc_delay_ms(uint32_t ms) {
    uint32_t i;
    if (!g_tsc_available || ms == 0) return;

    for (i = 0; i < ms; i++) {
        tsc_delay_us(1000);
    }
}

void timer_delay_us(uint32_t us) {
    if (us == 0) return;

    if (g_tsc_available) {
        tsc_delay_us(us);
    } else if (g_pm_timer_available) {
        acpi_pm_timer_delay_us(us);
    } else {
        pit_delay_ms((us + 999) / 1000);
    }
}

void timer_delay_ms(uint32_t ms) {
    if (ms == 0) return;

    if (g_tsc_available) {
        tsc_delay_ms(ms);
    } else if (g_pm_timer_available) {
        acpi_pm_timer_delay_ms(ms);
    } else {
        pit_delay_ms(ms);
    }
}
