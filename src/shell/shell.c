/*
 * This is free and unencumbered software released into the public domain.
 * GEMOS Preemptive Real-Time Operating System
 */

#include "shell.h"
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
#include "mmu.h"
#include "heap.h"
#include "pci.h"
#include "vga.h"
#include "pit.h"
#include "string.h"
#include "io.h"

#define CMD_BUFFER_SIZE 256
#define MAX_ARGS 16
#define HISTORY_SIZE 32

/* Command History State */
static char history[HISTORY_SIZE][CMD_BUFFER_SIZE];
static size_t history_count = 0;
static int history_browse_idx = -1;
static char current_draft[CMD_BUFFER_SIZE];

static void history_add(const char *cmd) {
    if (!cmd || cmd[0] == '\0') return;

    // Avoid duplicate of most recent entry
    if (history_count > 0) {
        size_t last_slot = (history_count - 1) % HISTORY_SIZE;
        if (strcmp(history[last_slot], cmd) == 0) {
            return;
        }
    }

    size_t slot = history_count % HISTORY_SIZE;
    strncpy(history[slot], cmd, CMD_BUFFER_SIZE - 1);
    history[slot][CMD_BUFFER_SIZE - 1] = '\0';
    history_count++;
}

static const char *history_get(size_t index) {
    if (index >= history_count) return NULL;
    size_t oldest = (history_count > HISTORY_SIZE) ? (history_count - HISTORY_SIZE) : 0;
    if (index < oldest) return NULL;
    return history[index % HISTORY_SIZE];
}

static void cmd_help(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    kprintf("\n==================== GEMOS RTOS Commands ====================\n");
    kprintf("  help                  - Show this help message\n");
    kprintf("  history               - Show command history list\n");
    kprintf("  !n / !!               - Re-execute command by history number / last\n");
    kprintf("  ps                    - List all active RTOS tasks\n");
    kprintf("  mem                   - Display memory and heap statistics\n");
    kprintf("  pci                   - List all PCI bus devices\n");
    kprintf("  lsusb                 - List USB devices (xHCI, HID, Hub, MSC)\n");
    kprintf("  storage               - List USB Mass Storage devices\n");
    kprintf("  readsec <dev> <lba>   - Read and hexdump a block from storage\n");
    kprintf("  writesec <dev> <lba> <s> - Write text into a block on storage\n");
    kprintf("  fatls [dev]           - List files on FAT filesystem (default: usb0)\n");
    kprintf("  fatcat [dev] <file>   - Read file content from FAT (default: usb0)\n");
    kprintf("  mouse                 - Show current USB mouse coordinates\n");
    kprintf("  bench                 - Run RTOS context-switch benchmark\n");
    kprintf("  uptime                - Show system uptime\n");
    kprintf("  clear                 - Clear the terminal screen\n");
    kprintf("  reboot                - Reboot the system\n");
    kprintf("=============================================================\n");
}

static void cmd_history(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    kprintf("\n--- Command History ---\n");
    if (history_count == 0) {
        kprintf("  (No commands in history)\n");
        return;
    }

    size_t start = (history_count > HISTORY_SIZE) ? (history_count - HISTORY_SIZE) : 0;
    for (size_t i = start; i < history_count; i++) {
        kprintf("  %3u  %s\n", (uint32_t)(i + 1), history[i % HISTORY_SIZE]);
    }
}

static void cmd_ps(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    kprintf("\n  ID  Name              State    Pri  Runtime(ms)  Stack Size\n");
    kprintf("  ----------------------------------------------------------\n");

    size_t count = rtos_get_task_count();
    for (size_t i = 0; i < count; i++) {
        task_t *t = rtos_get_task_by_index(i);
        if (!t) continue;

        kprintf("  %2u  %-16s  %-7s  %2u   %10u   %6u B\n",
                t->id, t->name, rtos_task_state_str(t->state),
                t->priority, rtos_ticks_to_ms(t->runtime_ticks),
                t->stack_size);
    }
}

static void cmd_mem(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    size_t total_p = pmm_get_total_pages();
    size_t free_p = pmm_get_free_pages();
    size_t used_p = pmm_get_used_pages();

    size_t h_total = 0, h_used = 0, h_free = 0;
    heap_stats(&h_total, &h_used, &h_free);

    kprintf("\n--- Physical Memory ---\n");
    kprintf("  Total: %u MB (%u pages)\n", (uint32_t)((total_p * 4) / 1024), total_p);
    kprintf("  Used:  %u MB (%u pages)\n", (uint32_t)((used_p * 4) / 1024), used_p);
    kprintf("  Free:  %u MB (%u pages)\n", (uint32_t)((free_p * 4) / 1024), free_p);

    kprintf("\n--- Kernel Heap ---\n");
    kprintf("  Total: %u KB\n", (uint32_t)(h_total / 1024));
    kprintf("  Used:  %u KB\n", (uint32_t)(h_used / 1024));
    kprintf("  Free:  %u KB\n", (uint32_t)(h_free / 1024));
}

static void cmd_pci(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    kprintf("\n  Bus:Slot.Fn  Vendor:Device  Class  Description\n");
    kprintf("  --------------------------------------------------------------\n");

    size_t count = pci_get_device_count();
    for (size_t i = 0; i < count; i++) {
        pci_device_t *d = pci_get_device(i);
        if (!d) continue;

        kprintf("  %02x:%02x.%u    %04x:%04x       %02x    %s\n",
                d->bus, d->slot, d->func, d->vendor_id, d->device_id,
                d->class_code, pci_class_to_string(d->class_code, d->subclass, d->prog_if));
    }
}

static void cmd_lsusb(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    kprintf("\n--- USB Device Tree (xHCI) ---\n");

    size_t count = usb_get_device_count();
    if (count == 0) {
        kprintf("  No USB devices detected.\n");
        return;
    }

    for (size_t i = 0; i < count; i++) {
        usb_device_t *dev = usb_get_device_by_index(i);
        if (!dev) continue;

        if (dev->parent_hub_slot == 0) {
            kprintf("  [Slot %u] (Root Port %u) %s\n", dev->slot_id, dev->root_port, dev->name);
        } else {
            kprintf("  |-- [Slot %u] (Hub %u Port %u, Route 0x%x) %s\n",
                    dev->slot_id, dev->parent_hub_slot, dev->parent_port, dev->route_string, dev->name);
        }

        kprintf("      Speed: %s | VID: 0x%04x PID: 0x%04x | Class: 0x%02x Sub: 0x%02x\n",
                usb_speed_to_string(dev->speed), dev->dev_desc.idVendor, dev->dev_desc.idProduct,
                dev->dev_desc.bDeviceClass, dev->dev_desc.bDeviceSubClass);

        for (uint8_t if_idx = 0; if_idx < dev->num_interfaces; if_idx++) {
            usb_interface_t *iface = &dev->interfaces[if_idx];
            kprintf("      Interface %u: Class 0x%02x (%s), %u Endpoints\n",
                    iface->interface_number, iface->interface_class,
                    usb_class_to_string(iface->interface_class), iface->num_endpoints);

            for (uint8_t ep_idx = 0; ep_idx < iface->num_endpoints; ep_idx++) {
                usb_endpoint_t *ep = &iface->endpoints[ep_idx];
                const char *dir = (ep->address & 0x80) ? "IN" : "OUT";
                kprintf("        EP 0x%02x (%s, DCI %u): MaxPkt=%u Interval=%u\n",
                        ep->address, dir, ep->dci, ep->max_packet_size, ep->interval);
            }
        }
    }
}

static void cmd_storage(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    kprintf("\n--- Storage Devices ---\n");

    size_t count = blockdev_count();
    if (count == 0) {
        kprintf("  No storage block devices found.\n");
        return;
    }

    for (size_t i = 0; i < count; i++) {
        block_dev_t *bdev = blockdev_get_by_index(i);
        if (!bdev) continue;

        kprintf("  Device: %s\n", bdev->name);
        kprintf("    Total Blocks: %u\n", bdev->total_blocks);
        kprintf("    Block Size:   %u bytes\n", bdev->block_size);
        kprintf("    Capacity:     %u MB (%u KB)\n",
                (uint32_t)(((uint64_t)bdev->total_blocks * bdev->block_size) / (1024 * 1024)),
                (uint32_t)(((uint64_t)bdev->total_blocks * bdev->block_size) / 1024));
    }
}

static uint32_t parse_int(const char *str) {
    uint32_t val = 0;
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
        while (*str) {
            char c = *str++;
            if (c >= '0' && c <= '9') val = val * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') val = val * 16 + (c - 'A' + 10);
            else break;
        }
    } else {
        while (*str >= '0' && *str <= '9') {
            val = val * 10 + (*str++ - '0');
        }
    }
    return val;
}

static void cmd_readsec(int argc, char **argv) {
    if (argc < 3) {
        kprintf("Usage: readsec <dev_name> <lba>\nExample: readsec usb0 0\n");
        return;
    }

    const char *dev_name = argv[1];
    uint32_t lba = parse_int(argv[2]);

    block_dev_t *bdev = blockdev_get(dev_name);
    if (!bdev) {
        kprintf("Block device '%s' not found.\n", dev_name);
        return;
    }

    uint8_t buffer[512];
    memset(buffer, 0, sizeof(buffer));

    kprintf("Reading sector %u from %s...\n", lba, dev_name);
    int res = bdev->read(bdev, lba, 1, buffer);
    if (res != 0) {
        kprint_color(0x4F, "Read failed with error code %d\n", res);
        return;
    }

    hexdump(buffer, 512);
}

static void cmd_writesec(int argc, char **argv) {
    if (argc < 4) {
        kprintf("Usage: writesec <dev_name> <lba> <text>\nExample: writesec usb0 100 \"Hello USB!\"\n");
        return;
    }

    const char *dev_name = argv[1];
    uint32_t lba = parse_int(argv[2]);
    const char *text = argv[3];

    block_dev_t *bdev = blockdev_get(dev_name);
    if (!bdev) {
        kprintf("Block device '%s' not found.\n", dev_name);
        return;
    }

    uint8_t buffer[512];
    memset(buffer, 0, sizeof(buffer));
    strncpy((char*)buffer, text, sizeof(buffer) - 1);

    kprintf("Writing text to sector %u on %s...\n", lba, dev_name);
    int res = bdev->write(bdev, lba, 1, buffer);
    if (res != 0) {
        kprint_color(0x4F, "Write failed with error code %d\n", res);
        return;
    }

    kprintf("Write successful. Reading back for verification:\n");
    memset(buffer, 0, sizeof(buffer));
    bdev->read(bdev, lba, 1, buffer);
    hexdump(buffer, 64);
}

static void cmd_fatls(int argc, char **argv) {
    const char *dev_name = (argc > 1) ? argv[1] : "usb0";
    block_dev_t *bdev = blockdev_get(dev_name);
    if (!bdev) {
        kprint_color(0x4F, "Block device '%s' not found.\n", dev_name);
        return;
    }

    fat_fs_t fs;
    if (fat_mount(bdev, &fs) != 0) {
        kprint_color(0x4F, "Failed to mount FAT filesystem on '%s'.\n", dev_name);
        return;
    }

    fat_list_root(&fs);
}

static void cmd_fatcat(int argc, char **argv) {
    const char *dev_name = "usb0";
    const char *filename = NULL;

    if (argc == 2) {
        filename = argv[1];
    } else if (argc >= 3) {
        dev_name = argv[1];
        filename = argv[2];
    } else {
        kprintf("Usage: fatcat [dev] <filename>\nExample: fatcat README.TXT or fatcat usb0 README.TXT\n");
        return;
    }

    block_dev_t *bdev = blockdev_get(dev_name);
    if (!bdev) {
        kprint_color(0x4F, "Block device '%s' not found.\n", dev_name);
        return;
    }

    fat_fs_t fs;
    if (fat_mount(bdev, &fs) != 0) {
        kprint_color(0x4F, "Failed to mount FAT filesystem on '%s'.\n", dev_name);
        return;
    }

    size_t buf_size = 65536;
    char *buf = (char*)kmalloc(buf_size);
    if (!buf) {
        kprint_color(0x4F, "Failed to allocate buffer for file read.\n");
        return;
    }

    size_t out_len = 0;
    if (fat_read_file(&fs, filename, buf, buf_size - 1, &out_len) == 0) {
        kprintf("\n--- %s (%u bytes) ---\n", filename, (uint32_t)out_len);
        for (size_t i = 0; i < out_len; i++) {
            char ch = buf[i];
            if (ch == '\r') continue;
            if (ch == '\n' || (ch >= 32 && ch <= 126) || ch == '\t') {
                vga_putc(ch);
                serial_putc(ch);
            } else {
                vga_putc('.');
                serial_putc('.');
            }
        }
        kprintf("\n--- End of file ---\n");
    } else {
        kprint_color(0x4F, "File '%s' not found on '%s'.\n", filename, dev_name);
    }

    kfree(buf);
}

static void cmd_mouse(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    int32_t x = 0, y = 0;
    uint8_t buttons = 0;
    usb_mouse_get_state(&x, &y, &buttons);
    kprintf("USB Mouse Position: X=%d, Y=%d | Buttons: Left=%s Right=%s Middle=%s\n",
            x, y,
            (buttons & 1) ? "Pressed" : "Released",
            (buttons & 2) ? "Pressed" : "Released",
            (buttons & 4) ? "Pressed" : "Released");
}

static volatile uint32_t bench_counter = 0;
static void bench_worker(void *arg) {
    UNUSED(arg);
    for (int i = 0; i < 50000; i++) {
        bench_counter++;
        rtos_yield();
    }
}

static void cmd_bench(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    kprintf("\nRunning RTOS Context Switch Benchmark (100,000 switches)...\n");

    bench_counter = 0;
    uint32_t start_time = pit_get_ticks();

    rtos_task_create("bench1", bench_worker, NULL, RTOS_PRIORITY_NORMAL, 4096);
    rtos_task_create("bench2", bench_worker, NULL, RTOS_PRIORITY_NORMAL, 4096);

    while (bench_counter < 100000) {
        rtos_yield();
    }

    uint32_t end_time = pit_get_ticks();
    uint32_t elapsed_ms = end_time - start_time;
    if (elapsed_ms == 0) elapsed_ms = 1;

    uint32_t switches_per_sec = (100000 * 1000) / elapsed_ms;
    kprintf("Done! Time: %u ms | Rate: %u context switches/sec\n", elapsed_ms, switches_per_sec);
}

static void cmd_uptime(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    uint32_t sec = pit_get_uptime_sec();
    uint32_t ms = pit_get_uptime_ms();
    kprintf("Uptime: %u seconds (%u ms, %u ticks)\n", sec, ms, pit_get_ticks());
}

static void cmd_clear(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    vga_clear();
}

static void cmd_reboot(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    kprintf("Rebooting system...\n");
    outb(0x64, 0xFE);
    __asm__ volatile ("lidt (0); int $3");
}

void shell_execute_command(char *cmd_line) {
    // 1. Expand history reference (!n or !!)
    char expanded[CMD_BUFFER_SIZE];
    expanded[0] = '\0';

    if (cmd_line[0] == '!' && history_count > 0) {
        if (cmd_line[1] == '!') {
            const char *last = history_get(history_count - 1);
            if (last) {
                strncpy(expanded, last, sizeof(expanded) - 1);
                kprintf("%s\n", expanded);
            }
        } else {
            uint32_t num = parse_int(&cmd_line[1]);
            if (num >= 1 && num <= history_count) {
                const char *entry = history_get(num - 1);
                if (entry) {
                    strncpy(expanded, entry, sizeof(expanded) - 1);
                    kprintf("%s\n", expanded);
                }
            } else {
                kprint_color(0x4F, "History event !%u not found.\n", num);
                return;
            }
        }
    } else {
        strncpy(expanded, cmd_line, sizeof(expanded) - 1);
    }

    expanded[sizeof(expanded) - 1] = '\0';
    if (strlen(expanded) == 0) return;

    // Add to history
    history_add(expanded);

    char *argv[MAX_ARGS];
    int argc = 0;

    char *p = expanded;
    while (*p && argc < MAX_ARGS) {
        while (*p == ' ') p++;
        if (*p == '\0') break;

        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ') p++;
            if (*p == ' ') *p++ = '\0';
        }
    }

    if (argc == 0) return;

    if (strcmp(argv[0], "help") == 0) cmd_help(argc, argv);
    else if (strcmp(argv[0], "history") == 0) cmd_history(argc, argv);
    else if (strcmp(argv[0], "ps") == 0) cmd_ps(argc, argv);
    else if (strcmp(argv[0], "mem") == 0) cmd_mem(argc, argv);
    else if (strcmp(argv[0], "pci") == 0) cmd_pci(argc, argv);
    else if (strcmp(argv[0], "lsusb") == 0) cmd_lsusb(argc, argv);
    else if (strcmp(argv[0], "storage") == 0) cmd_storage(argc, argv);
    else if (strcmp(argv[0], "readsec") == 0) cmd_readsec(argc, argv);
    else if (strcmp(argv[0], "writesec") == 0) cmd_writesec(argc, argv);
    else if (strcmp(argv[0], "fatls") == 0) cmd_fatls(argc, argv);
    else if (strcmp(argv[0], "fatcat") == 0) cmd_fatcat(argc, argv);
    else if (strcmp(argv[0], "mouse") == 0) cmd_mouse(argc, argv);
    else if (strcmp(argv[0], "bench") == 0) cmd_bench(argc, argv);
    else if (strcmp(argv[0], "uptime") == 0) cmd_uptime(argc, argv);
    else if (strcmp(argv[0], "clear") == 0) cmd_clear(argc, argv);
    else if (strcmp(argv[0], "reboot") == 0) cmd_reboot(argc, argv);
    else {
        kprint_color(0x4F, "Unknown command: '%s'. Type 'help' for commands.\n", argv[0]);
    }
}

static char shell_get_char(void) {
    if (usb_kbd_has_char()) {
        return usb_kbd_getchar();
    }
    if (serial_has_char()) {
        char c = serial_getchar();
        // Parse ANSI Escape Sequence: ESC [ A / B / C / D
        if (c == 27) { // ESC
            if (serial_has_char()) {
                char c2 = serial_getchar();
                if (c2 == '[') {
                    if (serial_has_char()) {
                        char c3 = serial_getchar();
                        if (c3 == 'A') return KEY_UP;
                        if (c3 == 'B') return KEY_DOWN;
                        if (c3 == 'C') return KEY_RIGHT;
                        if (c3 == 'D') return KEY_LEFT;
                    }
                }
            }
            return 0;
        }
        return c;
    }
    return 0;
}

void shell_task(void *arg) {
    UNUSED(arg);
    char cmd_buffer[CMD_BUFFER_SIZE];
    size_t cmd_pos = 0;
    cmd_buffer[0] = '\0';
    current_draft[0] = '\0';
    history_browse_idx = -1;

    kprint_color(0x0A, "\n=== GEMOS RTOS Interactive Console Ready ===\n");
    kprintf("gemos> ");

    while (1) {
        xhci_poll();

        char c = shell_get_char();
        if (c != 0) {
            if (c == '\n' || c == '\r') {
                kprintf("\n");
                cmd_buffer[cmd_pos] = '\0';
                if (cmd_pos > 0) {
                    shell_execute_command(cmd_buffer);
                    cmd_pos = 0;
                    cmd_buffer[0] = '\0';
                }
                history_browse_idx = -1;
                current_draft[0] = '\0';
                kprintf("gemos> ");
            } else if (c == KEY_UP) {
                // Navigate backwards in history
                if (history_count > 0) {
                    size_t oldest = (history_count > HISTORY_SIZE) ? (history_count - HISTORY_SIZE) : 0;
                    if (history_browse_idx == -1) {
                        cmd_buffer[cmd_pos] = '\0';
                        strncpy(current_draft, cmd_buffer, sizeof(current_draft) - 1);
                        history_browse_idx = (int)history_count - 1;
                    } else if (history_browse_idx > (int)oldest) {
                        history_browse_idx--;
                    }

                    // Erase current prompt line
                    while (cmd_pos > 0) {
                        kprintf("\b \b");
                        cmd_pos--;
                    }

                    const char *entry = history_get(history_browse_idx);
                    if (entry) {
                        strncpy(cmd_buffer, entry, sizeof(cmd_buffer) - 1);
                        cmd_pos = strlen(cmd_buffer);
                        kprintf("%s", cmd_buffer);
                    }
                }
            } else if (c == KEY_DOWN) {
                // Navigate forward in history
                if (history_browse_idx != -1) {
                    if (history_browse_idx < (int)history_count - 1) {
                        history_browse_idx++;

                        while (cmd_pos > 0) {
                            kprintf("\b \b");
                            cmd_pos--;
                        }

                        const char *entry = history_get(history_browse_idx);
                        if (entry) {
                            strncpy(cmd_buffer, entry, sizeof(cmd_buffer) - 1);
                            cmd_pos = strlen(cmd_buffer);
                            kprintf("%s", cmd_buffer);
                        }
                    } else {
                        // Restore draft line
                        history_browse_idx = -1;

                        while (cmd_pos > 0) {
                            kprintf("\b \b");
                            cmd_pos--;
                        }

                        strncpy(cmd_buffer, current_draft, sizeof(cmd_buffer) - 1);
                        cmd_pos = strlen(cmd_buffer);
                        kprintf("%s", cmd_buffer);
                    }
                }
            } else if (c == '\b' || c == 127) {
                if (cmd_pos > 0) {
                    cmd_pos--;
                    cmd_buffer[cmd_pos] = '\0';
                    kprintf("\b \b");
                }
            } else if ((uint8_t)c >= 32 && (uint8_t)c <= 126) {
                if (cmd_pos < CMD_BUFFER_SIZE - 1) {
                    cmd_buffer[cmd_pos++] = c;
                    cmd_buffer[cmd_pos] = '\0';
                    kprintf("%c", c);
                }
            }
        } else {
            rtos_sleep_ms(5);
        }
    }
}
