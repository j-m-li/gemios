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
#include "acpi.h"
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

    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint32_t framebuffer_addr_low;
    uint32_t framebuffer_addr_hi;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED 0
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB     1
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT     2
    uint8_t framebuffer_type;
    uint8_t framebuffer_red_field_position;/*uint32_t framebuffer_palette_addr;*/
    uint8_t framebuffer_red_mask_size;
    uint8_t framebuffer_green_field_position;
    uint8_t framebuffer_green_mask_size;
    uint8_t framebuffer_blue_field_position;/*uint16_t framebuffer_palette_num_colors;*/
    uint8_t framebuffer_blue_mask_size;
};
static void usb_worker_task(void *arg) {
    UNUSED(arg);
    while (1) {
        xhci_poll();
        usb_hid_poll();
        usb_hub_poll();
        rtos_sleep_ms(8); /* 125 Hz USB polling */
    }
}

static void telemetry_task(void *arg) {
    char buf[32];
    uint8_t header_color;

    UNUSED(arg);
    while (1) {
        snprintf(buf, sizeof(buf), "Uptime: %us", pit_get_uptime_sec());
        header_color = vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
        vga_puts_at(buf, header_color, 65, 0);

        rtos_sleep_ms(1000);
    }
}

void kmain(uint32_t magic, struct multiboot_info *mbi) {
    uint32_t mem_kb;
    phys_addr_t heap_mem;
    pci_device_t *xhci_dev;
    uint32_t framebuffer_palette_addr;
    uint16_t framebuffer_palette_num_colors;

    UNUSED(magic);

    framebuffer_palette_addr = *((uint32_t*)(&mbi->framebuffer_red_field_position));
    framebuffer_palette_num_colors = *((uint16_t*)(&mbi->framebuffer_blue_field_position));
 
   /* 1. Initialize Display and Serial Log */
    vga_init();

    kprint_color(0x0E, "\n=======================================================\n");
    kprint_color(0x0E, "   GEMIOS - 32-bit x86 Preemptive Real-Time OS          \n");
    kprint_color(0x0E, "   xHCI USB Host Controller, HID, Hub, Mass Storage    \n");
    kprint_color(0x0E, "=======================================================\n\n");

    /* 2. Initialize Core CPU Descriptors & Interrupts */
    kprintf("[Kernel] Initializing GDT...\n");
    gdt_init();

    kprintf("[Kernel] Initializing PIC 8259...\n");
    pic_init();

    kprintf("[Kernel] Initializing IDT...\n");
    idt_init();

    kprintf("[Kernel] Initializing PIT 8254 Timer (1000 Hz)...\n");
    pit_init(1000);

    /* 3. Initialize PS/2 Keyboard Controller */
    ps2_kbd_init();

    /* 4. Initialize Memory Managers */
    {
        uint32_t mmap_addr;
        uint32_t mmap_len;

        mmap_addr = 0;
        mmap_len = 0;
        if (mbi && (mbi->flags & (1 << 6))) {
            mmap_addr = mbi->mmap_addr;
            mmap_len = mbi->mmap_length;
        }
        mem_kb = (mbi && (mbi->flags & 0x01)) ? (mbi->mem_upper + 1024) : (128 * 1024);
        pmm_init(mem_kb, mmap_addr, mmap_len);
        kprintf("[Kernel] Initialized PMM (%u MB RAM, %u pages)\n",
                (uint32_t)((pmm_get_free_pages() * 4) / 1024), (uint32_t)pmm_get_total_pages());
    }

    /* Allocate 16 MB for Kernel Heap */
    heap_mem = pmm_alloc_pages(4096);
    heap_init((void*)heap_mem, 16 * 1024 * 1024);
    kprintf("[Kernel] Initialized 16 MB Kernel Heap at %p\n", (void*)heap_mem);

    /* 5. Initialize Block Device Subsystem & USB Core */
    blockdev_init();
    usb_core_init();

    /* 6. Scan PCI Bus and find xHCI Controller */
    kprintf("[Kernel] Scanning PCI bus...\n");
    pci_init();
    kprintf("[Kernel] Found %u PCI devices\n", (uint32_t)pci_get_device_count());

    xhci_dev = pci_find_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, PCI_PROGIF_XHCI);
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

    /* 7. Initialize ACPI Subsystem */
    acpi_init();

    /* 8. Initialize RTOS Preemptive Scheduler and Tasks */
    kprintf("[Kernel] Initializing RTOS Preemptive Scheduler...\n");
    rtos_sched_init();

    /* Create RTOS Tasks */
    rtos_task_create("usb_worker", usb_worker_task, NULL, RTOS_PRIORITY_HIGH, 8192);
    rtos_task_create("telemetry", telemetry_task, NULL, RTOS_PRIORITY_LOW, 4096);
    rtos_task_create("shell", shell_task, NULL, RTOS_PRIORITY_NORMAL, 16384);

    kprint_color(0x0A, "\n[Kernel] System Initialization Complete. Starting RTOS Scheduler!\n");

    /* 8. Start RTOS Preemptive Multitasking */
    rtos_sched_start();
}
