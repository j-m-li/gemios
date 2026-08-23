/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "ps2_kbd.h"
#include "idt.h"
#include "pic.h"
#include "io.h"
#include "vga.h"
#include "usb_hid.h"
#include "string.h"

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT 0x64

/* US QWERTY Scan Code Set 1 (Normal) */
static const char ps2_ascii_normal[128] = {
    0,   27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   /* Control */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   /* Left Shift */
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0,   /* Right Shift */
    '*',
    0,   /* Alt */
    ' ', /* Space */
    0,   /* Caps lock */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* F1 - F10 */
    0,   /* Num lock */
    0,   /* Scroll lock */
    0,   /* Home */
    0,   /* Up */
    0,   /* Page Up */
    '-',
    0,   /* Left */
    0,
    0,   /* Right */
    '+',
    0,   /* End */
    0,   /* Down */
    0,   /* Page Down */
    0,   /* Insert */
    0,   /* Delete */
    0, 0, 0,
    0,   /* F11 */
    0,   /* F12 */
    0
};

/* US QWERTY Scan Code Set 1 (Shifted) */
static const char ps2_ascii_shift[128] = {
    0,   27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   /* Control */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   /* Left Shift */
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0,   /* Right Shift */
    '*',
    0,   /* Alt */
    ' ', /* Space */
    0,   /* Caps lock */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* F1 - F10 */
    0,   /* Num lock */
    0,   /* Scroll lock */
    0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0,
    0,   /* F11 */
    0,   /* F12 */
    0
};

static void ps2_wait_write(void) {
    int timeout;
    timeout = 50000;
    while ((inb(PS2_STATUS_PORT) & 0x02) && --timeout) {
        io_wait();
    }
}

static void ps2_wait_read(void) {
    int timeout;
    timeout = 50000;
    while (!(inb(PS2_STATUS_PORT) & 0x01) && --timeout) {
        io_wait();
    }
}

static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool caps_lock = false;
static bool extended_code = false;

static void ps2_keyboard_irq_handler(registers_t *regs) {
    uint8_t scancode;
    bool released;
    uint8_t key;

    UNUSED(regs);

    scancode = inb(PS2_DATA_PORT);

    if (scancode == 0xE0) {
        extended_code = true;
        return;
    }

    released = (scancode & 0x80) != 0;
    key = scancode & 0x7F;

    if (extended_code) {
        extended_code = false;
        if (key == 0x1D) {
            /* Right Ctrl */
            ctrl_pressed = !released;
            return;
        }
        if (!released) {
            if (key == 0x48) kbd_push_char(KEY_UP);
            else if (key == 0x50) kbd_push_char(KEY_DOWN);
            else if (key == 0x4B) kbd_push_char(KEY_LEFT);
            else if (key == 0x4D) kbd_push_char(KEY_RIGHT);
            else if (key == 0x47) kbd_push_char(KEY_HOME);
            else if (key == 0x4F) kbd_push_char(KEY_END);
            else if (key == 0x49) kbd_push_char(KEY_PGUP);
            else if (key == 0x51) kbd_push_char(KEY_PGDN);
            else if (key == 0x53) kbd_push_char(KEY_DELETE);
        }
        return;
    }

    if (key == 0x1D) {
        /* Left Ctrl */
        ctrl_pressed = !released;
        return;
    }

    if (key == 0x2A || key == 0x36) {
        /* Left Shift or Right Shift */
        shift_pressed = !released;
        return;
    }

    if (key == 0x3A && !released) {
        /* Caps Lock toggle */
        caps_lock = !caps_lock;
        return;
    }

    if (!released) {
        if (key == 0x3B) { kbd_push_char(KEY_F1); return; }
        if (key == 0x3C) { kbd_push_char(KEY_F2); return; }
        if (key == 0x3D) { kbd_push_char(KEY_F3); return; }
        if (key == 0x01) { kbd_push_char(KEY_ESC); return; }

        if (key < 128) {
            bool use_shift;
            char c;

            use_shift = shift_pressed;
            c = use_shift ? ps2_ascii_shift[key] : ps2_ascii_normal[key];

            if (ctrl_pressed && c != 0) {
                if (c >= 'a' && c <= 'z') {
                    kbd_push_char((char)(c - 'a' + 1));
                    return;
                } else if (c >= 'A' && c <= 'Z') {
                    kbd_push_char((char)(c - 'A' + 1));
                    return;
                }
            }

            if (caps_lock && c >= 'a' && c <= 'z' && !shift_pressed) {
                c = c - 'a' + 'A';
            } else if (caps_lock && c >= 'A' && c <= 'Z' && shift_pressed) {
                c = c - 'A' + 'a';
            }

            if (c != 0) {
                kbd_push_char(c);
            }
        }
    }
}

void ps2_kbd_init(void) {
    uint8_t config;

    shift_pressed = false;
    caps_lock = false;
    extended_code = false;

    /* 1. Flush any pending data in PS/2 buffer */
    while (inb(PS2_STATUS_PORT) & 0x01) {
        inb(PS2_DATA_PORT);
    }

    /* 2. Read Controller Configuration Byte */
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0x20);
    ps2_wait_read();
    config = inb(PS2_DATA_PORT);

    /* 3. Enable IRQ1 (bit 0), enable translation (bit 6), enable clock (clear bit 4) */
    config |= (1 << 0);
    config &= ~(1 << 4);
    config |= (1 << 6);

    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0x60);
    ps2_wait_write();
    outb(PS2_DATA_PORT, config);

    /* 4. Enable First PS/2 Port */
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0xAE);

    /* 5. Register IRQ1 handler (Vector 33) and unmask IRQ1 in PIC */
    register_interrupt_handler(33, ps2_keyboard_irq_handler);
    pic_unmask_irq(1);

    /* 6. Enable Keyboard Scanning command (0xF4) */
    ps2_wait_write();
    outb(PS2_DATA_PORT, 0xF4);
    ps2_wait_read();
    inb(PS2_DATA_PORT); /* Read ACK */

    kprintf("[PS/2] Initialized PS/2 Keyboard Driver (IRQ1 Enabled)\n");
}
