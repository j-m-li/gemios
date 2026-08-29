/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "ps2.h"
#include "ps2_kbd.h"
#include "ps2_mouse.h"
#include "idt.h"
#include "pic.h"
#include "io.h"
#include "vga.h"
#include "string.h"

/* US QWERTY Scan Code Set 1 (Normal) */
static const char ps2_ascii_normal[128] = {
    0,   27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   /* Left Control */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   /* Left Shift */
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0,   /* Right Shift */
    '*',
    0,   /* Left Alt */
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
    0,   /* Left Control */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   /* Left Shift */
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0,   /* Right Shift */
    '*',
    0,   /* Left Alt */
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

static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;
static bool caps_lock = false;
static bool num_lock = true;
static bool scroll_lock = false;
static bool extended_code = false;
static bool g_ps2_kbd_present = false;

bool ps2_kbd_is_present(void) {
    return g_ps2_kbd_present;
}

void ps2_kbd_set_leds(bool scroll, bool num, bool caps) {
    uint8_t leds;
    leds = 0;
    if (scroll) leds |= (1 << 0);
    if (num)    leds |= (1 << 1);
    if (caps)   leds |= (1 << 2);

    ps2_wait_write();
    outb(PS2_DATA_PORT, PS2_KBD_CMD_SET_LEDS);
    if (ps2_wait_read()) {
        inb(PS2_DATA_PORT); /* Read ACK */
        ps2_wait_write();
        outb(PS2_DATA_PORT, leds);
        if (ps2_wait_read()) {
            inb(PS2_DATA_PORT); /* Read ACK */
        }
    }
}

void ps2_keyboard_handle_byte(uint8_t scancode) {
    bool released;
    uint8_t key;

    if (scancode == 0xE0) {
        extended_code = true;
        return;
    }

    /* Handle Pause/Break prefix 0xE1 (ignore sequence) */
    if (scancode == 0xE1) {
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
        if (key == 0x38) {
            /* Right Alt */
            alt_pressed = !released;
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
            else if (key == 0x1C) kbd_push_char('\n'); /* Keypad Enter */
            else if (key == 0x35) kbd_push_char('/');  /* Keypad / */
        }
        return;
    }

    if (key == 0x1D) {
        /* Left Ctrl */
        ctrl_pressed = !released;
        return;
    }

    if (key == 0x38) {
        /* Left Alt */
        alt_pressed = !released;
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
        ps2_kbd_set_leds(scroll_lock, num_lock, caps_lock);
        return;
    }

    if (key == 0x45 && !released) {
        /* Num Lock toggle */
        num_lock = !num_lock;
        ps2_kbd_set_leds(scroll_lock, num_lock, caps_lock);
        return;
    }

    if (key == 0x46 && !released) {
        /* Scroll Lock toggle */
        scroll_lock = !scroll_lock;
        ps2_kbd_set_leds(scroll_lock, num_lock, caps_lock);
        return;
    }

    if (!released) {
        if (key == 0x3B) { kbd_push_char(KEY_F1); return; }
        if (key == 0x3C) { kbd_push_char(KEY_F2); return; }
        if (key == 0x3D) { kbd_push_char(KEY_F3); return; }
        if (key == 0x01) { kbd_push_char(KEY_ESC); return; }

        /* Keypad with / without NumLock */
        if (key == 0x47) { kbd_push_char(num_lock ? '7' : KEY_HOME); return; }
        if (key == 0x48) { kbd_push_char(num_lock ? '8' : KEY_UP); return; }
        if (key == 0x49) { kbd_push_char(num_lock ? '9' : KEY_PGUP); return; }
        if (key == 0x4A) { kbd_push_char('-'); return; }
        if (key == 0x4B) { kbd_push_char(num_lock ? '4' : KEY_LEFT); return; }
        if (key == 0x4C) { if (num_lock) kbd_push_char('5'); return; }
        if (key == 0x4D) { kbd_push_char(num_lock ? '6' : KEY_RIGHT); return; }
        if (key == 0x4E) { kbd_push_char('+'); return; }
        if (key == 0x4F) { kbd_push_char(num_lock ? '1' : KEY_END); return; }
        if (key == 0x50) { kbd_push_char(num_lock ? '2' : KEY_DOWN); return; }
        if (key == 0x51) { kbd_push_char(num_lock ? '3' : KEY_PGDN); return; }
        if (key == 0x52) { if (num_lock) kbd_push_char('0'); return; }
        if (key == 0x53) { kbd_push_char(num_lock ? '.' : KEY_DELETE); return; }

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

static void ps2_keyboard_irq_handler(registers_t *regs) {
    uint8_t status;
    uint8_t data;

    UNUSED(regs);

    while ((status = inb(PS2_STATUS_PORT)) & PS2_STATUS_OUTPUT_FULL) {
        data = inb(PS2_DATA_PORT);

        if (status & PS2_STATUS_MOUSE_BUFFER_FULL) {
            /* Byte is from PS/2 mouse */
            ps2_mouse_handle_byte(data);
        } else {
            /* Byte is from PS/2 keyboard */
            ps2_keyboard_handle_byte(data);
        }
    }
}

void ps2_kbd_init(void) {
    shift_pressed = false;
    ctrl_pressed = false;
    alt_pressed = false;
    caps_lock = false;
    num_lock = true;
    scroll_lock = false;
    extended_code = false;

    /* 1. Enable First PS/2 Port (Keyboard) */
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, PS2_CMD_ENABLE_PORT1);

    /* 2. Register IRQ1 handler (Vector 33) and unmask IRQ1 in PIC */
    register_interrupt_handler(33, ps2_keyboard_irq_handler);
    pic_unmask_irq(1);

    /* 3. Enable Keyboard Scanning command (0xF4) */
    ps2_wait_write();
    outb(PS2_DATA_PORT, PS2_KBD_CMD_ENABLE_SCAN);
    if (ps2_wait_read()) {
        inb(PS2_DATA_PORT); /* Read ACK */
    }

    g_ps2_kbd_present = true;
    kprintf("[PS/2] Initialized PS/2 Keyboard Driver (IRQ1 Enabled)\n");
}
