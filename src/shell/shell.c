/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "shell.h"
#include "task.h"
#include "sched.h"
#include "time.h"
#include "usb_core.h"
#include "usb_hub.h"
#include "usb_hid.h"
#include "usb_msc.h"
#include "usb_audio.h"
#include "xhci.h"
#include "blockdev.h"
#include "fat.h"
#include "editor.h"
#include "mmu.h"
#include "heap.h"
#include "pci.h"
#include "vga.h"
#include "pit.h"
#include "string.h"
#include "acpi.h"
#include "ps2_mouse.h"
#include "ps2_kbd.h"
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
    size_t slot;

    if (!cmd || cmd[0] == '\0') return;

    /* Avoid duplicate of most recent entry */
    if (history_count > 0) {
        size_t last_slot;
        last_slot = (history_count - 1) % HISTORY_SIZE;
        if (strcmp(history[last_slot], cmd) == 0) {
            return;
        }
    }

    slot = history_count % HISTORY_SIZE;
    strncpy(history[slot], cmd, CMD_BUFFER_SIZE - 1);
    history[slot][CMD_BUFFER_SIZE - 1] = '\0';
    history_count++;
}

static const char *history_get(size_t index) {
    size_t oldest;

    if (index >= history_count) return NULL;
    oldest = (history_count > HISTORY_SIZE) ? (history_count - HISTORY_SIZE) : 0;
    if (index < oldest) return NULL;
    return history[index % HISTORY_SIZE];
}

static void cmd_help(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    kprintf("\n==================== GEMIOS RTOS Commands ====================\n");
    kprintf("  help                  - Show this help message\n");
    kprintf("  history               - Show command history list\n");
    kprintf("  !n / !!               - Re-execute command by history number / last\n");
    kprintf("  ps                    - List all active RTOS tasks\n");
    kprintf("  mem                   - Display memory and heap statistics\n");
    kprintf("  pci                   - List all PCI bus devices\n");
    kprintf("  lsusb                 - List USB devices (xHCI, HID, Hub, MSC)\n");
    kprintf("  rescan                - Rescan USB ports and USB Hubs for new devices\n");
    kprintf("  storage               - List USB Mass Storage devices\n");
    kprintf("  readsec <dev> <lba>   - Read and hexdump a block from storage\n");
    kprintf("  writesec <dev> <lba> <s> - Write text into a block on storage\n");
    kprintf("  cd [dev] [dir]        - Change current directory on FAT filesystem\n");
    kprintf("  pwd                   - Print current working directory\n");
    kprintf("  ls [dev] [dir]        - List directory contents on FAT (default: usb0)\n");
    kprintf("  cat [dev] <path>      - Read file content from FAT (default: usb0)\n");
    kprintf("  mkdir [dev] <dir>     - Create a directory in FAT filesystem\n");
    kprintf("  cp [-r] [src] <dest>  - Copy files or directories on FAT filesystem\n");
    kprintf("  rm [-r] [dev] <path>  - Remove files or directories (e.g. rm -r *)\n");
    kprintf("  edit [dev] <path>     - Fullscreen MS-DOS style UTF-8 text editor\n");
    kprintf("  audio [info|vol|mute] - Configure USB Audio (UAC 1.0)\n");
    kprintf("  play [dev] <file.wav> - Play WAV audio file through USB Audio\n");
    kprintf("  beep [freq] [ms]      - Play a sound tone through USB Audio\n");
    kprintf("  mouse                 - Show current mouse coordinates (PS/2 & USB)\n");
    kprintf("  bench                 - Run RTOS context-switch benchmark\n");
    kprintf("  uptime                - Show system uptime\n");
    kprintf("  clear                 - Clear the terminal screen\n");
    kprintf("  reboot                - Reboot the system\n");
    kprintf("  shutdown              - Power off / shutdown the system\n");
    kprintf("=============================================================\n");
}

static void cmd_history(int argc, char **argv) {
    size_t start;
    size_t i;

    UNUSED(argc);
    UNUSED(argv);
    kprintf("\n--- Command History ---\n");
    if (history_count == 0) {
        kprintf("  (No commands in history)\n");
        return;
    }

    start = (history_count > HISTORY_SIZE) ? (history_count - HISTORY_SIZE) : 0;
    for (i = start; i < history_count; i++) {
        kprintf("  %3u  %s\n", (uint32_t)(i + 1), history[i % HISTORY_SIZE]);
    }
}

static void cmd_ps(int argc, char **argv) {
    size_t count;
    size_t i;

    UNUSED(argc);
    UNUSED(argv);
    kprintf("\n  ID  Name              State    Pri  Runtime(ms)  Stack Size\n");
    kprintf("  ----------------------------------------------------------\n");

    count = rtos_get_task_count();
    for (i = 0; i < count; i++) {
        task_t *t;
        t = rtos_get_task_by_index(i);
        if (!t) continue;

        kprintf("  %2u  %-16s  %-7s  %2u   %10u   %6u B\n",
                t->id, t->name, rtos_task_state_str(t->state),
                t->priority, rtos_ticks_to_ms(t->runtime_ticks),
                t->stack_size);
    }
}

static void cmd_mem(int argc, char **argv) {
    size_t total_p;
    size_t free_p;
    size_t used_p;
    size_t h_total;
    size_t h_used;
    size_t h_free;

    UNUSED(argc);
    UNUSED(argv);

    total_p = pmm_get_total_pages();
    free_p = pmm_get_free_pages();
    used_p = pmm_get_used_pages();

    h_total = 0;
    h_used = 0;
    h_free = 0;
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
    size_t count;
    size_t i;

    UNUSED(argc);
    UNUSED(argv);
    kprintf("\n  Bus:Slot.Fn  Vendor:Device  Class  Description\n");
    kprintf("  --------------------------------------------------------------\n");

    count = pci_get_device_count();
    for (i = 0; i < count; i++) {
        pci_device_t *d;
        d = pci_get_device(i);
        if (!d) continue;

        kprintf("  %02x:%02x.%u    %04x:%04x       %02x    %s\n",
                d->bus, d->slot, d->func, d->vendor_id, d->device_id,
                d->class_code, pci_class_to_string(d->class_code, d->subclass, d->prog_if));
    }
}

static void cmd_lsusb(int argc, char **argv) {
    size_t count;
    size_t i;

    UNUSED(argc);
    UNUSED(argv);
    kprintf("\n--- USB Device Tree (xHCI) ---\n");

    count = usb_get_device_count();
    if (count == 0) {
        kprintf("  No USB devices detected.\n");
        return;
    }

    for (i = 0; i < count; i++) {
        usb_device_t *dev;
        uint8_t if_idx;

        dev = usb_get_device_by_index(i);
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

        for (if_idx = 0; if_idx < dev->num_interfaces; if_idx++) {
            usb_interface_t *iface;
            uint8_t ep_idx;

            iface = &dev->interfaces[if_idx];
            kprintf("      Interface %u: Class 0x%02x (%s), %u Endpoints\n",
                    iface->interface_number, iface->interface_class,
                    usb_class_to_string(iface->interface_class), iface->num_endpoints);

            for (ep_idx = 0; ep_idx < iface->num_endpoints; ep_idx++) {
                usb_endpoint_t *ep;
                const char *dir;

                ep = &iface->endpoints[ep_idx];
                dir = (ep->address & 0x80) ? "IN" : "OUT";
                kprintf("        EP 0x%02x (%s, DCI %u): MaxPkt=%u Interval=%u\n",
                        ep->address, dir, ep->dci, ep->max_packet_size, ep->interval);
            }
        }
    }
}

static void cmd_rescan(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    kprintf("Rescanning USB xHCI ports and Hubs...\n");
    xhci_scan_ports(xhci_get_controller());
    usb_hub_poll();
    kprintf("USB rescan complete. Detected %u USB device(s), %u storage block device(s).\n",
            (uint32_t)usb_get_device_count(), (uint32_t)blockdev_count());
}

static void cmd_storage(int argc, char **argv) {
    size_t count;
    size_t i;

    UNUSED(argc);
    UNUSED(argv);
    kprintf("\n--- Storage Devices ---\n");

    count = blockdev_count();
    if (count == 0) {
        kprintf("  No storage block devices found.\n");
        return;
    }

    for (i = 0; i < count; i++) {
        block_dev_t *bdev;
        bdev = blockdev_get_by_index(i);
        if (!bdev) continue;

        kprintf("  Device: %s\n", bdev->name);
        kprintf("    Total Blocks: %u\n", bdev->total_blocks);
        kprintf("    Block Size:   %u bytes\n", bdev->block_size);
        kprintf("    Capacity:     %u MB (%u KB)\n",
                ((bdev->total_blocks / 1024) * bdev->block_size) / 1024,
                ((bdev->total_blocks / 1024) * bdev->block_size) + ((bdev->total_blocks % 1024) * bdev->block_size) / 1024);
    }
}

static uint32_t parse_int(const char *str) {
    uint32_t val;
    val = 0;

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
    const char *dev_name;
    uint32_t lba;
    block_dev_t *bdev;
    uint8_t buffer[512];
    int res;

    if (argc < 3) {
        kprintf("Usage: readsec <dev_name> <lba>\nExample: readsec usb0 0\n");
        return;
    }

    dev_name = argv[1];
    lba = parse_int(argv[2]);

    bdev = blockdev_get(dev_name);
    if (!bdev) {
        kprintf("Block device '%s' not found.\n", dev_name);
        return;
    }

    memset(buffer, 0, sizeof(buffer));

    kprintf("Reading sector %u from %s...\n", lba, dev_name);
    res = bdev->read(bdev, lba, 1, buffer);
    if (res != 0) {
        kprint_color(0x4F, "Read failed with error code %d\n", res);
        return;
    }

    hexdump(buffer, 512);
}

static void cmd_writesec(int argc, char **argv) {
    const char *dev_name;
    uint32_t lba;
    const char *text;
    block_dev_t *bdev;
    uint8_t buffer[512];
    int res;

    if (argc < 4) {
        kprintf("Usage: writesec <dev_name> <lba> <text>\nExample: writesec usb0 100 \"Hello USB!\"\n");
        return;
    }

    dev_name = argv[1];
    lba = parse_int(argv[2]);
    text = argv[3];

    bdev = blockdev_get(dev_name);
    if (!bdev) {
        kprintf("Block device '%s' not found.\n", dev_name);
        return;
    }

    memset(buffer, 0, sizeof(buffer));
    strncpy((char*)buffer, text, sizeof(buffer) - 1);

    kprintf("Writing text to sector %u on %s...\n", lba, dev_name);
    res = bdev->write(bdev, lba, 1, buffer);
    if (res != 0) {
        kprint_color(0x4F, "Write failed with error code %d\n", res);
        return;
    }

    kprintf("Write successful. Reading back for verification:\n");
    memset(buffer, 0, sizeof(buffer));
    bdev->read(bdev, lba, 1, buffer);
    hexdump(buffer, 64);
}

static char g_shell_cwd[256] = "/";
static char g_shell_dev[32] = "usb0";

static void shell_build_path(const char *in_path, char *out_path, size_t out_max) {
    if (!in_path || in_path[0] == '\0') {
        strncpy(out_path, g_shell_cwd, out_max - 1);
        out_path[out_max - 1] = '\0';
        return;
    }
    if (in_path[0] == '/' || in_path[0] == '\\') {
        strncpy(out_path, in_path, out_max - 1);
        out_path[out_max - 1] = '\0';
        return;
    }
    if (strcmp(g_shell_cwd, "/") == 0) {
        snprintf(out_path, out_max, "/%s", in_path);
    } else {
        snprintf(out_path, out_max, "%s/%s", g_shell_cwd, in_path);
    }
}

static void cmd_cd(int argc, char **argv) {
    const char *dev_name;
    const char *target;
    block_dev_t *bdev;
    fat_fs_t fs;
    char full_path[256];

    dev_name = g_shell_dev;
    target = "/";

    if (argc == 2) {
        if (blockdev_get(argv[1]) != NULL) {
            dev_name = argv[1];
            target = "/";
        } else {
            target = argv[1];
        }
    } else if (argc >= 3) {
        dev_name = argv[1];
        target = argv[2];
    }

    bdev = blockdev_get(dev_name);
    if (!bdev) {
        kprint_color(0x4F, "Block device '%s' not found.\n", dev_name);
        return;
    }

    if (fat_mount(bdev, &fs) != 0) {
        kprint_color(0x4F, "Failed to mount FAT filesystem on '%s'.\n", dev_name);
        return;
    }

    shell_build_path(target, full_path, sizeof(full_path));

    if (fat_is_dir(&fs, full_path) != 1) {
        kprint_color(0x4F, "Directory '%s' not found on '%s'.\n", target, dev_name);
        return;
    }

    strncpy(g_shell_dev, dev_name, sizeof(g_shell_dev) - 1);
    if (strcmp(target, "/") == 0) {
        strcpy(g_shell_cwd, "/");
    } else if (target[0] == '/') {
        strncpy(g_shell_cwd, target, sizeof(g_shell_cwd) - 1);
    } else if (strcmp(target, "..") == 0) {
        char *last;
        last = strrchr(g_shell_cwd, '/');
        if (last && last != g_shell_cwd) {
            *last = '\0';
        } else {
            strcpy(g_shell_cwd, "/");
        }
    } else if (strcmp(target, ".") == 0) {
        /* No-op */
    } else {
        strncpy(g_shell_cwd, full_path, sizeof(g_shell_cwd) - 1);
    }
    g_shell_cwd[sizeof(g_shell_cwd) - 1] = '\0';
}

static void cmd_pwd(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    kprintf("%s:%s\n", g_shell_dev, g_shell_cwd);
}

static void cmd_ls(int argc, char **argv) {
    const char *dev_name;
    const char *target;
    block_dev_t *bdev;
    fat_fs_t fs;
    char full_path[256];
    int res;

    dev_name = g_shell_dev;
    target = g_shell_cwd;

    if (argc == 2) {
        if (argv[1][0] == '/' || argv[1][0] == '\\') {
            target = argv[1];
        } else {
            /* Check if it's a directory on current device first */
            block_dev_t *cur_bdev;
            bool is_dir;

            cur_bdev = blockdev_get(g_shell_dev);
            is_dir = false;
            if (cur_bdev) {
                fat_fs_t cur_fs;
                if (fat_mount(cur_bdev, &cur_fs) == 0) {
                    char cur_full_path[256];
                    shell_build_path(argv[1], cur_full_path, sizeof(cur_full_path));
                    if (fat_is_dir(&cur_fs, cur_full_path) == 1) {
                        is_dir = true;
                        target = argv[1];
                    }
                }
            }
            if (!is_dir) {
                if (blockdev_get(argv[1]) != NULL) {
                    dev_name = argv[1];
                    target = "/";
                } else {
                    kprint_color(0x4F, "Block device '%s' not found.\n", argv[1]);
                    return;
                }
            }
        }
    } else if (argc >= 3) {
        dev_name = argv[1];
        target = argv[2];
    }

    bdev = blockdev_get(dev_name);
    if (!bdev) {
        kprint_color(0x4F, "Block device '%s' not found.\n", dev_name);
        return;
    }

    if (fat_mount(bdev, &fs) != 0) {
        kprint_color(0x4F, "Failed to mount FAT filesystem on '%s'.\n", dev_name);
        return;
    }

    shell_build_path(target, full_path, sizeof(full_path));

    res = fat_list_dir(&fs, full_path);
    if (res < 0) {
        kprint_color(0x4F, "Failed to read directory '%s' from '%s'.\n", target, dev_name);
    }
}

static void cmd_cat(int argc, char **argv) {
    const char *dev_name;
    const char *filename;
    block_dev_t *bdev;
    fat_fs_t fs;
    char full_path[256];
    size_t buf_size;
    char *buf;
    size_t out_len;

    dev_name = g_shell_dev;
    filename = NULL;

    if (argc == 2) {
        filename = argv[1];
    } else if (argc >= 3) {
        dev_name = argv[1];
        filename = argv[2];
    } else {
        kprintf("Usage: cat [dev] <filename>\nExample: cat README.TXT or cat usb0 README.TXT\n");
        return;
    }

    bdev = blockdev_get(dev_name);
    if (!bdev) {
        kprint_color(0x4F, "Block device '%s' not found.\n", dev_name);
        return;
    }

    if (fat_mount(bdev, &fs) != 0) {
        kprint_color(0x4F, "Failed to mount FAT filesystem on '%s'.\n", dev_name);
        return;
    }

    shell_build_path(filename, full_path, sizeof(full_path));

    buf_size = 65536;
    buf = (char*)kmalloc(buf_size);
    if (!buf) {
        kprint_color(0x4F, "Failed to allocate buffer for file read.\n");
        return;
    }

    out_len = 0;
    if (fat_read_file(&fs, full_path, buf, buf_size - 1, &out_len) == 0) {
        size_t i;
        kprintf("\n--- %s (%u bytes) ---\n", filename, (uint32_t)out_len);
        i = 0;
        while (i < out_len) {
            char ch;
            ch = buf[i];
            if (ch == '\r') {
                i++;
                continue;
            }
            if (ch == '\n' || ch == '\t') {
                vga_putc(ch);
                serial_putc(ch);
                i++;
            } else if ((uint8_t)ch < 128) {
                if (ch >= 32 && ch <= 126) {
                    vga_putc(ch);
                    serial_putc(ch);
                } else {
                    vga_putc('.');
                    serial_putc('.');
                }
                i++;
            } else {
                /* Multi-byte UTF-8 character */
                uint32_t cp;
                int c_len;
                uint8_t glyph;
                int k;

                cp = 0;
                c_len = utf8_decode(&buf[i], &cp);
                if (c_len <= 0) c_len = 1;

                glyph = utf8_to_cp437(cp);
                vga_putc((char)glyph);

                /* Pass full UTF-8 byte sequence to serial */
                for (k = 0; k < c_len && i + k < out_len; k++) {
                    serial_putc(buf[i + k]);
                }
                i += c_len;
            }
        }
        kprintf("\n--- End of file ---\n");
    } else {
        kprint_color(0x4F, "File '%s' not found on '%s'.\n", filename, dev_name);
    }

    kfree(buf);
}

static void cmd_mkdir(int argc, char **argv) {
    const char *dev_name;
    const char *dirname;
    block_dev_t *bdev;
    fat_fs_t fs;
    char full_path[256];
    int res;

    dev_name = g_shell_dev;
    dirname = NULL;

    if (argc == 2) {
        dirname = argv[1];
    } else if (argc >= 3) {
        dev_name = argv[1];
        dirname = argv[2];
    } else {
        kprintf("Usage: mkdir [dev] <dirname>\nExample: mkdir DOCS or mkdir usb0 TESTDIR\n");
        return;
    }

    bdev = blockdev_get(dev_name);
    if (!bdev) {
        kprint_color(0x4F, "Block device '%s' not found.\n", dev_name);
        return;
    }

    if (fat_mount(bdev, &fs) != 0) {
        kprint_color(0x4F, "Failed to mount FAT filesystem on '%s'.\n", dev_name);
        return;
    }

    shell_build_path(dirname, full_path, sizeof(full_path));

    res = fat_mkdir(&fs, full_path);
    if (res == 0) {
        kprint_color(0x0A, "Directory '%s' created successfully on '%s'.\n", dirname, dev_name);
    } else if (res == -2) {
        kprint_color(0x4F, "Directory or file '%s' already exists on '%s'.\n", dirname, dev_name);
    } else {
        kprint_color(0x4F, "Failed to create directory '%s' on '%s' (code %d).\n", dirname, dev_name, res);
    }
}

static void parse_dev_and_filepath(const char *arg, const char *default_dev, const char *default_cwd,
                                   char *out_dev, size_t dev_max, char *out_path, size_t path_max) {
    const char *colon;
    const char *rel_path;

    strncpy(out_dev, default_dev, dev_max - 1);
    out_dev[dev_max - 1] = '\0';

    colon = strchr(arg, ':');
    if (colon && colon != arg) {
        size_t dev_len = (size_t)(colon - arg);
        if (dev_len < dev_max) {
            memcpy(out_dev, arg, dev_len);
            out_dev[dev_len] = '\0';
        }
        rel_path = colon + 1;
    } else {
        rel_path = arg;
    }

    if (!rel_path || rel_path[0] == '\0') {
        strncpy(out_path, default_cwd, path_max - 1);
    } else if (rel_path[0] == '/' || rel_path[0] == '\\') {
        strncpy(out_path, rel_path, path_max - 1);
    } else {
        if (strcmp(default_cwd, "/") == 0) {
            snprintf(out_path, path_max, "/%s", rel_path);
        } else {
            snprintf(out_path, path_max, "%s/%s", default_cwd, rel_path);
        }
    }
    out_path[path_max - 1] = '\0';
}

static void cmd_cp(int argc, char **argv) {
    bool recursive = false;
    char src_dev[32];
    char dst_dev[32];
    char src_path[256];
    char dst_path[256];
    const char *non_opts[8];
    int non_opt_cnt = 0;
    int i;
    block_dev_t *src_bdev;
    block_dev_t *dst_bdev;
    fat_fs_t src_fs;
    fat_fs_t dst_fs;
    uint32_t src_size = 0;
    uint8_t src_attr = 0;
    int res;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            const char *opt = &argv[i][1];
            while (*opt) {
                if (*opt == 'r' || *opt == 'R') recursive = true;
                opt++;
            }
        } else if (non_opt_cnt < 8) {
            non_opts[non_opt_cnt++] = argv[i];
        }
    }

    if (non_opt_cnt < 2) {
        kprintf("Usage: cp [-r] [src_dev] <src_path> [dst_dev] <dst_path>\n");
        kprintf("Examples:\n");
        kprintf("  cp README.TXT BACKUP.TXT\n");
        kprintf("  cp \"My Document.txt\" /DOCS/\n");
        kprintf("  cp usb0 README.TXT usb0 BACKUP.TXT\n");
        kprintf("  cp -r /FOLDER1 /FOLDER2\n");
        return;
    }

    if (non_opt_cnt == 2) {
        parse_dev_and_filepath(non_opts[0], g_shell_dev, g_shell_cwd, src_dev, sizeof(src_dev), src_path, sizeof(src_path));
        parse_dev_and_filepath(non_opts[1], g_shell_dev, g_shell_cwd, dst_dev, sizeof(dst_dev), dst_path, sizeof(dst_path));
    } else if (non_opt_cnt == 3) {
        if (blockdev_get(non_opts[0]) != NULL) {
            strncpy(src_dev, non_opts[0], sizeof(src_dev) - 1);
            src_dev[sizeof(src_dev) - 1] = '\0';
            strncpy(dst_dev, non_opts[0], sizeof(dst_dev) - 1);
            dst_dev[sizeof(dst_dev) - 1] = '\0';
            shell_build_path(non_opts[1], src_path, sizeof(src_path));
            shell_build_path(non_opts[2], dst_path, sizeof(dst_path));
        } else if (blockdev_get(non_opts[1]) != NULL) {
            parse_dev_and_filepath(non_opts[0], g_shell_dev, g_shell_cwd, src_dev, sizeof(src_dev), src_path, sizeof(src_path));
            strncpy(dst_dev, non_opts[1], sizeof(dst_dev) - 1);
            dst_dev[sizeof(dst_dev) - 1] = '\0';
            shell_build_path(non_opts[2], dst_path, sizeof(dst_path));
        } else {
            parse_dev_and_filepath(non_opts[0], g_shell_dev, g_shell_cwd, src_dev, sizeof(src_dev), src_path, sizeof(src_path));
            parse_dev_and_filepath(non_opts[1], g_shell_dev, g_shell_cwd, dst_dev, sizeof(dst_dev), dst_path, sizeof(dst_path));
        }
    } else {
        strncpy(src_dev, non_opts[0], sizeof(src_dev) - 1);
        src_dev[sizeof(src_dev) - 1] = '\0';
        shell_build_path(non_opts[1], src_path, sizeof(src_path));
        strncpy(dst_dev, non_opts[2], sizeof(dst_dev) - 1);
        dst_dev[sizeof(dst_dev) - 1] = '\0';
        shell_build_path(non_opts[3], dst_path, sizeof(dst_path));
    }

    src_bdev = blockdev_get(src_dev);
    if (!src_bdev) {
        kprint_color(0x4F, "Block device '%s' not found.\n", src_dev);
        return;
    }

    dst_bdev = blockdev_get(dst_dev);
    if (!dst_bdev) {
        kprint_color(0x4F, "Block device '%s' not found.\n", dst_dev);
        return;
    }

    if (fat_mount(src_bdev, &src_fs) != 0) {
        kprint_color(0x4F, "Failed to mount FAT filesystem on '%s'.\n", src_dev);
        return;
    }

    if (src_bdev == dst_bdev) {
        dst_fs = src_fs;
    } else {
        if (fat_mount(dst_bdev, &dst_fs) != 0) {
            kprint_color(0x4F, "Failed to mount FAT filesystem on '%s'.\n", dst_dev);
            return;
        }
    }

    if (fat_stat(&src_fs, src_path, &src_size, &src_attr) != 0) {
        kprint_color(0x4F, "cp: cannot stat '%s': No such file or directory\n", src_path);
        return;
    }

    /* If destination is a directory or path ends with '/', append source basename */
    if (fat_is_dir(&dst_fs, dst_path) == 1 || dst_path[strlen(dst_path) - 1] == '/') {
        const char *base = strrchr(src_path, '/');
        if (!base) base = strrchr(src_path, '\\');
        base = (base) ? base + 1 : src_path;

        size_t dlen = strlen(dst_path);
        if (dlen > 0 && dst_path[dlen - 1] == '/') {
            strncat(dst_path, base, sizeof(dst_path) - dlen - 1);
        } else {
            strncat(dst_path, "/", sizeof(dst_path) - dlen - 1);
            strncat(dst_path, base, sizeof(dst_path) - strlen(dst_path) - 1);
        }
    }

    if (src_attr & FAT_ATTR_DIRECTORY) {
        if (!recursive) {
            kprint_color(0x4F, "cp: -r not specified; omitting directory '%s'\n", src_path);
            return;
        }
        res = fat_copy_dir(&src_fs, src_path, &dst_fs, dst_path);
        if (res == 0) {
            kprint_color(0x0A, "Copied directory '%s' -> '%s' on '%s'.\n", src_path, dst_path, dst_dev);
        } else {
            kprint_color(0x4F, "Failed to copy directory '%s' to '%s' (code %d).\n", src_path, dst_path, res);
        }
        return;
    }

    res = fat_copy_file(&src_fs, src_path, &dst_fs, dst_path);
    if (res == 0) {
        kprint_color(0x0A, "Copied '%s' -> '%s' (%u bytes)\n", src_path, dst_path, src_size);
    } else {
        kprint_color(0x4F, "Failed to copy '%s' to '%s' (code %d).\n", src_path, dst_path, res);
    }
}

static void cmd_rm(int argc, char **argv) {
    bool recursive;
    bool force;
    const char *dev_name;
    const char *target;
    int i;
    block_dev_t *bdev;
    fat_fs_t fs;
    char full_path[256];
    int res;

    recursive = false;
    force = false;
    dev_name = g_shell_dev;
    target = NULL;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            const char *opt;
            opt = &argv[i][1];
            while (*opt) {
                if (*opt == 'r' || *opt == 'R') recursive = true;
                else if (*opt == 'f') force = true;
                opt++;
            }
        } else if (blockdev_get(argv[i]) != NULL && target == NULL && i + 1 < argc) {
            dev_name = argv[i];
        } else {
            target = argv[i];
        }
    }

    if (!target) {
        kprintf("Usage: rm [-r|-rf] [dev] <file|dir|pattern>\nExample: rm -r * or rm -r DOCS or rm README.TXT\n");
        return;
    }

    bdev = blockdev_get(dev_name);
    if (!bdev) {
        kprint_color(0x4F, "Block device '%s' not found.\n", dev_name);
        return;
    }

    if (fat_mount(bdev, &fs) != 0) {
        kprint_color(0x4F, "Failed to mount FAT filesystem on '%s'.\n", dev_name);
        return;
    }

    shell_build_path(target, full_path, sizeof(full_path));

    res = fat_remove(&fs, full_path, recursive);
    if (res > 0) {
        if (strchr(target, '*') != NULL || strchr(target, '?') != NULL) {
            kprint_color(0x0A, "Removed %d item(s) on '%s'.\n", res, dev_name);
        } else {
            kprint_color(0x0A, "Removed '%s' on '%s'.\n", target, dev_name);
        }
    } else if (res == -2) {
        kprint_color(0x4F, "rm: cannot remove '%s': Is a directory (use -r)\n", target);
    } else {
        if (!force) {
            kprint_color(0x4F, "rm: cannot remove '%s': No such file or directory\n", target);
        }
    }
}

static void cmd_audio(int argc, char **argv) {
    usb_audio_device_t *audio;
    size_t count;
    size_t i;

    count = usb_audio_get_device_count();
    if (count == 0) {
        kprint_color(0x4F, "No USB Audio device detected.\n");
        return;
    }

    audio = usb_audio_get_default();
    if (!audio) {
        kprint_color(0x4F, "No active USB Audio device.\n");
        return;
    }

    if (argc < 2 || strcmp(argv[1], "info") == 0 || strcmp(argv[1], "status") == 0) {
        kprintf("\n================ USB Audio Devices (%u) ================\n", (uint32_t)count);
        for (i = 0; i < count; i++) {
            usb_audio_device_t *a = usb_audio_get_device(i);
            if (!a) continue;
            kprintf("  Device %u: %s\n", (uint32_t)i, a->dev->name);
            kprintf("    Slot ID:         %u\n", a->slot_id);
            kprintf("    Playback Stream: Interface %u (Alt %u), Endpoint 0x%02x (DCI %u)\n",
                    a->as_out_iface, a->as_out_alt, a->as_out_ep_addr, a->as_out_ep_dci);
            kprintf("    Playback Rate:   %u Hz, %u (%s), %u-bit PCM\n",
                    a->sample_rate, a->channels, (a->channels == 2) ? "Stereo" : "Mono", a->bits_per_sample);
            kprintf("    Master Volume:   %u%% (%s)\n", a->volume_percent, a->is_muted ? "MUTED" : "UNMUTED");
            if (a->as_in_ep_dci != 0) {
                kprintf("    Capture Stream:  Interface %u (Alt %u), Endpoint 0x%02x (DCI %u)\n",
                        a->as_in_iface, a->as_in_alt, a->as_in_ep_addr, a->as_in_ep_dci);
                kprintf("    Capture Rate:    %u Hz, %u (%s), %u-bit PCM\n",
                        a->in_sample_rate, a->in_channels, (a->in_channels == 2) ? "Stereo" : "Mono", a->in_bits_per_sample);
                kprintf("    Microphone Vol:  %u%% (%s)\n", a->mic_volume_percent, a->mic_is_muted ? "MUTED" : "UNMUTED");
            }
            kprintf("    Buffering:       Triple Buffering (3x 40ms Periods, %u B/period)\n", (uint32_t)AUDIO_PERIOD_MAX_BYTES);
            kprintf("    Queue Status:    %u / 3 periods queued (Active: %s, Underruns: %u)\n",
                    a->tb.queued_periods, usb_audio_is_playing(a) ? "YES" : "NO", a->tb.underruns);
        }
        kprintf("========================================================\n");
        kprintf("Commands: audio vol <0-100> | audio mic vol <0-100> | audio mic mute <on|off> | record <file.wav> [sec] | play <file.wav>\n");
        return;
    }

    if (strcmp(argv[1], "vol") == 0 || strcmp(argv[1], "volume") == 0) {
        uint32_t v;
        if (argc < 3) {
            kprintf("Current master volume: %u%%\nUsage: audio vol <0-100>\n", audio->volume_percent);
            return;
        }
        v = parse_int(argv[2]);
        if (v > 100) v = 100;
        usb_audio_set_volume(audio, (uint8_t)v);
        kprint_color(0x0A, "Set USB Audio master volume to %u%%\n", v);
    } else if (strcmp(argv[1], "mute") == 0) {
        bool m = true;
        if (argc >= 3 && (strcmp(argv[2], "off") == 0 || strcmp(argv[2], "0") == 0)) {
            m = false;
        }
        usb_audio_set_mute(audio, m);
        kprint_color(0x0A, "USB Audio %s\n", m ? "MUTED" : "UNMUTED");
    } else if (strcmp(argv[1], "mic") == 0) {
        if (argc >= 3 && (strcmp(argv[2], "vol") == 0 || strcmp(argv[2], "volume") == 0)) {
            uint32_t mv;
            if (argc < 4) {
                kprintf("Current microphone volume: %u%%\nUsage: audio mic vol <0-100>\n", audio->mic_volume_percent);
                return;
            }
            mv = parse_int(argv[3]);
            if (mv > 100) mv = 100;
            usb_audio_set_mic_volume(audio, (uint8_t)mv);
            kprint_color(0x0A, "Set Microphone volume to %u%%\n", mv);
        } else if (argc >= 3 && strcmp(argv[2], "mute") == 0) {
            bool mm;
            if (argc < 4) {
                kprintf("Current microphone mute status: %s\nUsage: audio mic mute <on|off>\n", audio->mic_is_muted ? "MUTED" : "UNMUTED");
                return;
            }
            mm = (strcmp(argv[3], "off") != 0 && strcmp(argv[3], "0") != 0);
            usb_audio_set_mic_mute(audio, mm);
            kprint_color(0x0A, "Microphone %s\n", mm ? "MUTED" : "UNMUTED");
        } else {
            kprintf("Microphone Status: Volume: %u%% | Mute: %s\n",
                    audio->mic_volume_percent, audio->mic_is_muted ? "MUTED" : "UNMUTED");
            kprintf("Usage: audio mic vol <0-100> | audio mic mute <on|off>\n");
        }
    } else if (strcmp(argv[1], "stop") == 0) {
        usb_audio_stop_all();
        kprint_color(0x0A, "Stopped USB Audio playback.\n");
    } else if (strcmp(argv[1], "tone") == 0 || strcmp(argv[1], "beep") == 0) {
        uint32_t freq = (argc >= 3) ? parse_int(argv[2]) : 440;
        uint32_t dur = (argc >= 4) ? parse_int(argv[3]) : 500;
        kprintf("Playing %u Hz tone for %u ms...\n", freq, dur);
        usb_audio_play_tone(freq, dur);
    } else if (strcmp(argv[1], "test") == 0 || strcmp(argv[1], "bench") == 0) {
        uint32_t dur = (argc >= 3) ? parse_int(argv[2]) : 3000;
        kprintf("========================================================\n");
        kprintf("  GEMIOS USB Audio Hardware Continuous Stream Test       \n");
        kprintf("  Target Duration: %u ms (48000 Hz, Stereo 16-bit)      \n", dur);
        kprintf("========================================================\n");
        usb_audio_play_tone(440, dur);
        kprint_color(0x0A, "[PASS] Audio stream completed.\n");
    } else if (strcmp(argv[1], "play") == 0) {
        if (argc < 3) {
            kprintf("Usage: audio play [--bg] <file.wav>\n");
            return;
        }
        if (strcmp(argv[2], "--bg") == 0 && argc >= 4) {
            usb_audio_play_file_async(argv[3]);
        } else {
            usb_audio_play_file(argv[2]);
        }
    } else {
        kprintf("Unknown audio command: '%s'. Use 'audio info', 'audio vol', 'audio mute', 'audio mic vol', 'play <file.wav>', 'record <file.wav> [sec]'.\n", argv[1]);
    }
}

static void cmd_beep(int argc, char **argv) {
    uint32_t freq = 440;
    uint32_t dur = 300;
    if (argc >= 2) freq = parse_int(argv[1]);
    if (argc >= 3) dur = parse_int(argv[2]);
    kprintf("Playing beep tone (%u Hz, %u ms)...\n", freq, dur);
    usb_audio_play_tone(freq, dur);
}

static void cmd_play(int argc, char **argv) {
    const char *dev_name;
    const char *filename;
    char full_path[256];
    bool async_mode = false;
    int file_arg_idx = 1;

    dev_name = g_shell_dev;
    if (argc < 2) {
        kprintf("Usage: play [--bg] [dev] <file.wav>\nExample: play TEST.WAV or play --bg usb0 TEST.WAV\n");
        return;
    }

    if (strcmp(argv[1], "--bg") == 0 || strcmp(argv[1], "-b") == 0) {
        async_mode = true;
        file_arg_idx = 2;
    }

    if (file_arg_idx >= argc) {
        kprintf("Usage: play [--bg] [dev] <file.wav>\n");
        return;
    }

    if (file_arg_idx + 1 == argc) {
        filename = argv[file_arg_idx];
    } else {
        dev_name = argv[file_arg_idx];
        filename = argv[file_arg_idx + 1];
    }

    shell_build_path(filename, full_path, sizeof(full_path));

    if (async_mode) {
        kprintf("Starting background playback of '%s' on '%s'...\n", full_path, dev_name);
        usb_audio_play_file_dev_async(dev_name, full_path);
    } else {
        usb_audio_play_file_dev(dev_name, full_path);
    }
}

static void cmd_record(int argc, char **argv) {
    const char *dev_name = g_shell_dev;
    const char *filename;
    char full_path[256];
    uint32_t duration_sec = 5;

    if (argc < 2) {
        kprintf("Usage: record [dev] <file.wav> [seconds]\nExample: record REC.WAV 5\n");
        return;
    }

    if (argc == 2) {
        filename = argv[1];
    } else if (argc == 3) {
        if (argv[2][0] >= '0' && argv[2][0] <= '9') {
            filename = argv[1];
            duration_sec = parse_int(argv[2]);
        } else {
            dev_name = argv[1];
            filename = argv[2];
        }
    } else {
        dev_name = argv[1];
        filename = argv[2];
        duration_sec = parse_int(argv[3]);
    }

    if (duration_sec == 0) duration_sec = 5;
    if (duration_sec > 60) duration_sec = 60;

    if (!dev_name || dev_name[0] == '\0') dev_name = "usb0";
    shell_build_path(filename, full_path, sizeof(full_path));

    kprintf("Recording %u seconds of audio to '%s' on %s...\n", duration_sec, full_path, dev_name);
    usb_audio_record_wav_file(dev_name, full_path, duration_sec);
}

static void cmd_mouse(int argc, char **argv) {
    int32_t x;
    int32_t y;
    uint8_t buttons;
    bool found;

    UNUSED(argc);
    UNUSED(argv);

    found = false;

    if (ps2_mouse_is_present()) {
        x = 0;
        y = 0;
        buttons = 0;
        ps2_mouse_get_state(&x, &y, &buttons);
        kprintf("PS/2 Mouse Position: X=%d, Y=%d | Buttons: Left=%s Right=%s Middle=%s%s\n",
                x, y,
                (buttons & 1) ? "Pressed" : "Released",
                (buttons & 2) ? "Pressed" : "Released",
                (buttons & 4) ? "Pressed" : "Released",
                ps2_mouse_has_scroll_wheel() ? " [Wheel Enabled]" : "");
        found = true;
    }

    if (usb_mouse_is_present()) {
        x = 0;
        y = 0;
        buttons = 0;
        usb_mouse_get_state(&x, &y, &buttons);
        kprintf("USB Mouse Position: X=%d, Y=%d | Buttons: Left=%s Right=%s Middle=%s\n",
                x, y,
                (buttons & 1) ? "Pressed" : "Released",
                (buttons & 2) ? "Pressed" : "Released",
                (buttons & 4) ? "Pressed" : "Released");
        found = true;
    }

    if (!found) {
        x = 0;
        y = 0;
        buttons = 0;
        usb_mouse_get_state(&x, &y, &buttons);
        kprintf("USB Mouse Position: X=%d, Y=%d | Buttons: Left=%s Right=%s Middle=%s\n",
                x, y,
                (buttons & 1) ? "Pressed" : "Released",
                (buttons & 2) ? "Pressed" : "Released",
                (buttons & 4) ? "Pressed" : "Released");
    }
}

static volatile uint32_t bench_counter = 0;
static void bench_worker(void *arg) {
    int i;
    UNUSED(arg);
    for (i = 0; i < 50000; i++) {
        bench_counter++;
        rtos_yield();
    }
}

static void cmd_bench(int argc, char **argv) {
    uint32_t start_time;
    uint32_t end_time;
    uint32_t elapsed_ms;
    uint32_t switches_per_sec;

    UNUSED(argc);
    UNUSED(argv);
    kprintf("\nRunning RTOS Context Switch Benchmark (100,000 switches)...\n");

    bench_counter = 0;
    start_time = pit_get_ticks();

    rtos_task_create("bench1", bench_worker, NULL, RTOS_PRIORITY_NORMAL, 4096);
    rtos_task_create("bench2", bench_worker, NULL, RTOS_PRIORITY_NORMAL, 4096);

    while (bench_counter < 100000) {
        rtos_yield();
    }

    end_time = pit_get_ticks();
    elapsed_ms = end_time - start_time;
    if (elapsed_ms == 0) elapsed_ms = 1;

    switches_per_sec = (100000 * 1000) / elapsed_ms;
    kprintf("Done! Time: %u ms | Rate: %u context switches/sec\n", elapsed_ms, switches_per_sec);
}

static void cmd_uptime(int argc, char **argv) {
    uint32_t sec;
    uint32_t ms;

    UNUSED(argc);
    UNUSED(argv);
    sec = pit_get_uptime_sec();
    ms = pit_get_uptime_ms();
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
    kprintf("Rebooting system via ACPI...\n");
    acpi_reboot();
}

static void cmd_shutdown(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    kprintf("Shutting down system via ACPI...\n");
    acpi_poweroff();
}

static void cmd_edit(int argc, char **argv) {
    const char *dev_name;
    const char *filename;
    char full_path[256];

    dev_name = g_shell_dev;
    filename = "UNTITLED.TXT";

    if (argc == 2) {
        filename = argv[1];
    } else if (argc >= 3) {
        dev_name = argv[1];
        filename = argv[2];
    }

    shell_build_path(filename, full_path, sizeof(full_path));
    editor_open(dev_name, full_path);
}

void shell_execute_command(char *cmd_line) {
    char expanded[CMD_BUFFER_SIZE];
    char *argv[MAX_ARGS];
    int argc;
    char *p;

    /* 1. Expand history reference (!n or !!) */
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

    /* Add to history */
    history_add(expanded);

    argc = 0;
    p = expanded;
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
    else if (strcmp(argv[0], "rescan") == 0 || strcmp(argv[0], "usbrescan") == 0) cmd_rescan(argc, argv);
    else if (strcmp(argv[0], "storage") == 0) cmd_storage(argc, argv);
    else if (strcmp(argv[0], "readsec") == 0) cmd_readsec(argc, argv);
    else if (strcmp(argv[0], "writesec") == 0) cmd_writesec(argc, argv);
    else if (strcmp(argv[0], "cd") == 0) cmd_cd(argc, argv);
    else if (strcmp(argv[0], "pwd") == 0) cmd_pwd(argc, argv);
    else if (strcmp(argv[0], "ls") == 0 || strcmp(argv[0], "fatls") == 0) cmd_ls(argc, argv);
    else if (strcmp(argv[0], "cat") == 0 || strcmp(argv[0], "fatcat") == 0) cmd_cat(argc, argv);
    else if (strcmp(argv[0], "mkdir") == 0 || strcmp(argv[0], "fatmkdir") == 0) cmd_mkdir(argc, argv);
    else if (strcmp(argv[0], "cp") == 0 || strcmp(argv[0], "fatcp") == 0) cmd_cp(argc, argv);
    else if (strcmp(argv[0], "rm") == 0) cmd_rm(argc, argv);
    else if (strcmp(argv[0], "edit") == 0) cmd_edit(argc, argv);
    else if (strcmp(argv[0], "audio") == 0) cmd_audio(argc, argv);
    else if (strcmp(argv[0], "play") == 0) cmd_play(argc, argv);
    else if (strcmp(argv[0], "record") == 0) cmd_record(argc, argv);
    else if (strcmp(argv[0], "beep") == 0) cmd_beep(argc, argv);
    else if (strcmp(argv[0], "mouse") == 0) cmd_mouse(argc, argv);
    else if (strcmp(argv[0], "bench") == 0) cmd_bench(argc, argv);
    else if (strcmp(argv[0], "uptime") == 0) cmd_uptime(argc, argv);
    else if (strcmp(argv[0], "clear") == 0) cmd_clear(argc, argv);
    else if (strcmp(argv[0], "reboot") == 0) cmd_reboot(argc, argv);
    else if (strcmp(argv[0], "shutdown") == 0 || strcmp(argv[0], "poweroff") == 0) cmd_shutdown(argc, argv);
    else {
        kprint_color(0x4F, "Unknown command: '%s'. Type 'help' for commands.\n", argv[0]);
    }
}

static int get_serial_byte_timed(int loops) {
    while (!serial_has_char() && --loops > 0) {
        io_wait();
    }
    if (serial_has_char()) {
        return (uint8_t)serial_getchar();
    }
    return -1;
}

static int shell_get_char(void) {
    if (usb_kbd_has_char()) {
        return (int)usb_kbd_getchar();
    }
    if (serial_has_char()) {
        uint8_t c;
        c = (uint8_t)serial_getchar();
        /* Parse ANSI Escape Sequences */
        if (c == 27) { /* ESC */
            int c2;
            c2 = get_serial_byte_timed(500000);
            if (c2 == '[') {
                int c3;
                c3 = get_serial_byte_timed(500000);
                if (c3 == 'A') return KEY_UP;
                if (c3 == 'B') return KEY_DOWN;
                if (c3 == 'C') return KEY_RIGHT;
                if (c3 == 'D') return KEY_LEFT;
                if (c3 == 'H') return KEY_HOME;
                if (c3 == 'F') return KEY_END;
                if (c3 == '3') {
                    int c4 = get_serial_byte_timed(500000);
                    if (c4 == '~') return KEY_DELETE;
                }
                if (c3 == '1' || c3 == '7') {
                    int c4 = get_serial_byte_timed(500000);
                    if (c4 == '~') return KEY_HOME;
                }
                if (c3 == '4' || c3 == '8') {
                    int c4 = get_serial_byte_timed(500000);
                    if (c4 == '~') return KEY_END;
                }
            } else if (c2 == 'O') {
                int c3;
                c3 = get_serial_byte_timed(500000);
                if (c3 == 'A') return KEY_UP;
                if (c3 == 'B') return KEY_DOWN;
                if (c3 == 'C') return KEY_RIGHT;
                if (c3 == 'D') return KEY_LEFT;
                if (c3 == 'H') return KEY_HOME;
                if (c3 == 'F') return KEY_END;
            }
            if (c2 == -1) return 0;
            return c2;
        }
        return (int)c;
    }
    return 0;
}

void shell_task(void *arg) {
    char cmd_buffer[CMD_BUFFER_SIZE];
    size_t cmd_pos;
    size_t cmd_len;

    UNUSED(arg);
    cmd_pos = 0;
    cmd_len = 0;
    cmd_buffer[0] = '\0';
    current_draft[0] = '\0';
    history_browse_idx = -1;

    kprint_color(0x0A, "\n=== GEMIOS RTOS Interactive Console Ready ===\n");
    kprintf("gemios> ");

    while (1) {
        int c;

        xhci_poll();

        c = shell_get_char();
        if (c != 0) {
            if (c == '\n' || c == '\r') {
                kprintf("\n");
                cmd_buffer[cmd_len] = '\0';
                if (cmd_len > 0) {
                    shell_execute_command(cmd_buffer);
                    cmd_pos = 0;
                    cmd_len = 0;
                    cmd_buffer[0] = '\0';
                }
                history_browse_idx = -1;
                current_draft[0] = '\0';
                kprintf("gemios> ");
            } else if (c == KEY_UP) {
                /* Navigate backwards in history */
                if (history_count > 0) {
                    size_t oldest;
                    const char *entry;

                    oldest = (history_count > HISTORY_SIZE) ? (history_count - HISTORY_SIZE) : 0;
                    if (history_browse_idx == -1) {
                        cmd_buffer[cmd_len] = '\0';
                        strncpy(current_draft, cmd_buffer, sizeof(current_draft) - 1);
                        history_browse_idx = (int)history_count - 1;
                    } else if (history_browse_idx > (int)oldest) {
                        history_browse_idx--;
                    }

                    /* Move cursor to end then erase entire prompt line */
                    while (cmd_pos < cmd_len) {
                        kprintf("%c", cmd_buffer[cmd_pos++]);
                    }
                    while (cmd_pos > 0) {
                        kprintf("\b \b");
                        cmd_pos--;
                    }

                    entry = history_get(history_browse_idx);
                    if (entry) {
                        strncpy(cmd_buffer, entry, sizeof(cmd_buffer) - 1);
                        cmd_len = strlen(cmd_buffer);
                        cmd_pos = cmd_len;
                        kprintf("%s", cmd_buffer);
                    } else {
                        cmd_len = 0;
                        cmd_buffer[0] = '\0';
                    }
                }
            } else if (c == KEY_DOWN) {
                /* Navigate forward in history */
                if (history_browse_idx != -1) {
                    while (cmd_pos < cmd_len) {
                        kprintf("%c", cmd_buffer[cmd_pos++]);
                    }
                    while (cmd_pos > 0) {
                        kprintf("\b \b");
                        cmd_pos--;
                    }

                    if (history_browse_idx < (int)history_count - 1) {
                        const char *entry;
                        history_browse_idx++;
                        entry = history_get(history_browse_idx);
                        if (entry) {
                            strncpy(cmd_buffer, entry, sizeof(cmd_buffer) - 1);
                            cmd_len = strlen(cmd_buffer);
                            cmd_pos = cmd_len;
                            kprintf("%s", cmd_buffer);
                        } else {
                            cmd_len = 0;
                            cmd_buffer[0] = '\0';
                        }
                    } else {
                        /* Restore draft line */
                        history_browse_idx = -1;
                        strncpy(cmd_buffer, current_draft, sizeof(cmd_buffer) - 1);
                        cmd_len = strlen(cmd_buffer);
                        cmd_pos = cmd_len;
                        kprintf("%s", cmd_buffer);
                    }
                }
            } else if (c == KEY_LEFT) {
                if (cmd_pos > 0) {
                    cmd_pos--;
                    kprintf("\b");
                }
            } else if (c == KEY_RIGHT) {
                if (cmd_pos < cmd_len) {
                    kprintf("%c", cmd_buffer[cmd_pos]);
                    cmd_pos++;
                }
            } else if (c == KEY_HOME || c == 1) { /* Home or Ctrl+A */
                while (cmd_pos > 0) {
                    kprintf("\b");
                    cmd_pos--;
                }
            } else if (c == KEY_END || c == 5) { /* End or Ctrl+E */
                while (cmd_pos < cmd_len) {
                    kprintf("%c", cmd_buffer[cmd_pos]);
                    cmd_pos++;
                }
            } else if (c == 21 || c == 3) { /* Ctrl+U or Ctrl+C: clear line */
                while (cmd_pos < cmd_len) {
                    kprintf("%c", cmd_buffer[cmd_pos++]);
                }
                while (cmd_pos > 0) {
                    kprintf("\b \b");
                    cmd_pos--;
                }
                cmd_len = 0;
                cmd_buffer[0] = '\0';
            } else if (c == 11) { /* Ctrl+K: clear to end of line */
                size_t k;
                for (k = cmd_pos; k < cmd_len; k++) {
                    kprintf(" ");
                }
                for (k = cmd_pos; k < cmd_len; k++) {
                    kprintf("\b");
                }
                cmd_len = cmd_pos;
                cmd_buffer[cmd_len] = '\0';
            } else if (c == '\b' || c == 127) {
                if (cmd_pos > 0) {
                    if (cmd_pos == cmd_len) {
                        /* Backspace at end of line */
                        cmd_pos--;
                        cmd_len--;
                        cmd_buffer[cmd_len] = '\0';
                        kprintf("\b \b");
                    } else {
                        /* Backspace in middle of line */
                        size_t k;
                        for (k = cmd_pos - 1; k < cmd_len - 1; k++) {
                            cmd_buffer[k] = cmd_buffer[k + 1];
                        }
                        cmd_pos--;
                        cmd_len--;
                        cmd_buffer[cmd_len] = '\0';

                        kprintf("\b");
                        for (k = cmd_pos; k < cmd_len; k++) {
                            kprintf("%c", cmd_buffer[k]);
                        }
                        kprintf(" ");
                        for (k = cmd_pos; k <= cmd_len; k++) {
                            kprintf("\b");
                        }
                    }
                }
            } else if (c == KEY_DELETE) {
                if (cmd_pos < cmd_len) {
                    size_t k;
                    for (k = cmd_pos; k < cmd_len - 1; k++) {
                        cmd_buffer[k] = cmd_buffer[k + 1];
                    }
                    cmd_len--;
                    cmd_buffer[cmd_len] = '\0';

                    for (k = cmd_pos; k < cmd_len; k++) {
                        kprintf("%c", cmd_buffer[k]);
                    }
                    kprintf(" ");
                    for (k = cmd_pos; k <= cmd_len; k++) {
                        kprintf("\b");
                    }
                }
            } else if ((uint8_t)c >= 32 && (uint8_t)c <= 126) {
                if (cmd_len < CMD_BUFFER_SIZE - 1) {
                    if (cmd_pos == cmd_len) {
                        /* Append at end */
                        cmd_buffer[cmd_len++] = (char)c;
                        cmd_buffer[cmd_len] = '\0';
                        cmd_pos++;
                        kprintf("%c", c);
                    } else {
                        /* Insert in middle */
                        size_t k;
                        for (k = cmd_len; k > cmd_pos; k--) {
                            cmd_buffer[k] = cmd_buffer[k - 1];
                        }
                        cmd_buffer[cmd_pos] = (char)c;
                        cmd_len++;
                        cmd_buffer[cmd_len] = '\0';

                        for (k = cmd_pos; k < cmd_len; k++) {
                            kprintf("%c", cmd_buffer[k]);
                        }
                        cmd_pos++;
                        for (k = cmd_pos; k < cmd_len; k++) {
                            kprintf("\b");
                        }
                    }
                }
            }
        } else {
            rtos_sleep_ms(5);
        }
    }
}
