/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "types.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "ps2_kbd.h"
#include "mmu.h"
#include "heap.h"
#include "pci.h"
#include "vga.h"
#include "string.h"
#include "task.h"
#include "sched.h"
#include "time.h"
#include "usb_core.h"
#include "usb_hub.h"
#include "usb_hid.h"
#include "usb_msc.h"
#include "xhci.h"
#include "blockdev.h"
#include "fat.h"
#include "shell.h"
#include "io.h"

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} PACKED;

static void usb_worker_task(void *arg) {
    UNUSED(arg);
    while (1) {
        xhci_poll();
        usb_hid_poll();
        usb_hub_poll();
        rtos_sleep_ms(8); // 125 Hz USB polling
    }
}

static void telemetry_task(void *arg) {
    UNUSED(arg);
    while (1) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Uptime: %us", pit_get_uptime_sec());
        uint8_t header_color = vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
        vga_puts_at(buf, header_color, 65, 0);

        rtos_sleep_ms(1000);
    }
}

void kmain(uint32_t magic, struct multiboot_info *mbi) {
    UNUSED(magic);

    // 1. Initialize Display and Serial Log
    vga_init();

    kprint_color(0x0E, "\n=======================================================\n");
    kprint_color(0x0E, "   GEMIOS - 32-bit x86 Preemptive Real-Time OS          \n");
    kprint_color(0x0E, "   xHCI USB Host Controller, HID, Hub, Mass Storage    \n");
    kprint_color(0x0E, "=======================================================\n\n");

    // 2. Initialize Core CPU Descriptors & Interrupts
    kprintf("[Kernel] Initializing GDT...\n");
    gdt_init();

    kprintf("[Kernel] Initializing PIC 8259...\n");
    pic_init();

    kprintf("[Kernel] Initializing IDT...\n");
    idt_init();

    kprintf("[Kernel] Initializing PIT 8254 Timer (1000 Hz)...\n");
    pit_init(1000);

    // 3. Initialize PS/2 Keyboard Controller
    ps2_kbd_init();

    // 4. Initialize Memory Managers
    uint32_t mem_kb = (mbi && (mbi->flags & 0x01)) ? (mbi->mem_upper + 1024) : (128 * 1024);
    kprintf("[Kernel] Detected %u MB RAM. Initializing PMM...\n", mem_kb / 1024);
    pmm_init(mem_kb);

    // Allocate 16 MB for Kernel Heap
    phys_addr_t heap_mem = pmm_alloc_pages(4096);
    heap_init((void*)heap_mem, 16 * 1024 * 1024);
    kprintf("[Kernel] Initialized 16 MB Kernel Heap at %p\n", (void*)heap_mem);

    // 5. Initialize Block Device Subsystem & USB Core
    blockdev_init();
    usb_core_init();

    // 6. Scan PCI Bus and find xHCI Controller
    kprintf("[Kernel] Scanning PCI bus...\n");
    pci_init();
    kprintf("[Kernel] Found %u PCI devices\n", (uint32_t)pci_get_device_count());

    pci_device_t *xhci_dev = pci_find_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, PCI_PROGIF_XHCI);
    if (xhci_dev) {
        kprintf("[Kernel] Found USB xHCI Controller at %02x:%02x.%u (Vendor=0x%04x Device=0x%04x)\n",
                xhci_dev->bus, xhci_dev->slot, xhci_dev->func, xhci_dev->vendor_id, xhci_dev->device_id);

        if (xhci_init(xhci_dev)) {
            kprintf("[Kernel] Scanning Root Hub Ports & Enumerating Devices...\n");
            xhci_scan_ports(xhci_get_controller());
        }
    } else {
        kprint_color(0x4F, "[Kernel] No USB xHCI Controller found on PCI bus!\n");
    }

    // 7. Initialize RTOS Preemptive Scheduler and Tasks
    kprintf("[Kernel] Initializing RTOS Preemptive Scheduler...\n");
    rtos_sched_init();

    // Create RTOS Tasks
    rtos_task_create("usb_worker", usb_worker_task, NULL, RTOS_PRIORITY_HIGH, 8192);
    rtos_task_create("telemetry", telemetry_task, NULL, RTOS_PRIORITY_LOW, 4096);
    rtos_task_create("shell", shell_task, NULL, RTOS_PRIORITY_NORMAL, 16384);

    kprint_color(0x0A, "\n[Kernel] System Initialization Complete. Starting RTOS Scheduler!\n");

    // 8. Start RTOS Preemptive Multitasking
    rtos_sched_start();
}
