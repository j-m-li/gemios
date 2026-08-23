/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "usb_hid.h"
#include "xhci.h"
#include "vga.h"
#include "string.h"
#include "io.h"

#define MAX_HID_KBDS 4
#define MAX_HID_MICE 4
#define KBD_BUF_SIZE 256

static usb_hid_kbd_t kbds[MAX_HID_KBDS];
static size_t kbd_count = 0;

static usb_hid_mouse_t mice[MAX_HID_MICE];
static size_t mouse_count = 0;

/* Circular keyboard buffer */
static uint16_t kbd_buffer[KBD_BUF_SIZE];
static size_t kbd_head = 0;
static size_t kbd_tail = 0;

/* USB HID Scancode to ASCII/Keycode table (US Layout) */
static const uint16_t hid_scancode_unmodified[128] = {
    0, 0, 0, 0,
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '\n', 27, '\b', '\t', ' ', '-', '=', '[', ']', '\\',
    '#', ';', '\'', '`', ',', '.', '/',
    0, // Caps lock
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // F1..F12
    0, 0, 0, // PrintScreen, ScrollLock, Pause
    0, // Insert
    KEY_HOME, // 0x4A Home
    0, // PageUp
    KEY_DELETE, // 0x4C Delete
    KEY_END, // 0x4D End
    0, // PageDown
    KEY_RIGHT, // 0x4F Right Arrow
    KEY_LEFT,  // 0x50 Left Arrow
    KEY_DOWN,  // 0x51 Down Arrow
    KEY_UP,    // 0x52 Up Arrow
};

static const uint16_t hid_scancode_shift[128] = {
    0, 0, 0, 0,
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '\n', 27, '\b', '\t', ' ', '_', '+', '{', '}', '|',
    '~', ':', '"', '~', '<', '>', '?',
    0, // Caps lock
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0,
    0,
    KEY_HOME,
    0,
    KEY_DELETE,
    KEY_END,
    0,
    KEY_RIGHT,
    KEY_LEFT,
    KEY_DOWN,
    KEY_UP,
};

void kbd_push_char(uint16_t key) {
    uint32_t flags = irq_save();
    size_t next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next != kbd_tail) {
        kbd_buffer[kbd_head] = key;
        kbd_head = next;
    }
    irq_restore(flags);
}

bool usb_kbd_has_char(void) {
    return kbd_head != kbd_tail;
}

uint16_t usb_kbd_getchar(void) {
    uint32_t flags = irq_save();
    if (kbd_head == kbd_tail) {
        irq_restore(flags);
        return 0;
    }
    uint16_t c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    irq_restore(flags);
    return c;
}

static void process_kbd_report(usb_hid_kbd_t *kbd, usb_kbd_report_t *report) {
    bool shift = (report->modifiers & 0x22) != 0; // Left or Right Shift
    bool ctrl  = (report->modifiers & 0x11) != 0; // Left or Right Ctrl

    for (int i = 0; i < 6; i++) {
        uint8_t key = report->keycodes[i];
        if (key == 0) continue;

        bool is_new = true;
        for (int j = 0; j < 6; j++) {
            if (kbd->last_report.keycodes[j] == key) {
                is_new = false;
                break;
            }
        }

        if (is_new) {
            if (key == 0x29) { kbd_push_char(KEY_ESC); continue; }
            if (key == 0x3A) { kbd_push_char(KEY_F1); continue; }
            if (key == 0x3B) { kbd_push_char(KEY_F2); continue; }
            if (key == 0x3C) { kbd_push_char(KEY_F3); continue; }
            if (key == 0x4A) { kbd_push_char(KEY_HOME); continue; }
            if (key == 0x4D) { kbd_push_char(KEY_END); continue; }
            if (key == 0x4B) { kbd_push_char(KEY_PGUP); continue; }
            if (key == 0x4E) { kbd_push_char(KEY_PGDN); continue; }
            if (key == 0x4C) { kbd_push_char(KEY_DELETE); continue; }

            if (key < 128) {
                uint16_t c = shift ? hid_scancode_shift[key] : hid_scancode_unmodified[key];
                if (ctrl && c != 0) {
                    if (c >= 'a' && c <= 'z') {
                        kbd_push_char((uint16_t)(c - 'a' + 1));
                        continue;
                    } else if (c >= 'A' && c <= 'Z') {
                        kbd_push_char((uint16_t)(c - 'A' + 1));
                        continue;
                    }
                }
                if (c) {
                    kbd_push_char(c);
                }
            }
        }
    }

    memcpy(&kbd->last_report, report, sizeof(usb_kbd_report_t));
}

static void process_mouse_report(usb_hid_mouse_t *mouse, usb_mouse_report_t *report) {
    mouse->x += report->dx;
    mouse->y += report->dy;

    if (mouse->x < 0) mouse->x = 0;
    if (mouse->x >= VGA_WIDTH) mouse->x = VGA_WIDTH - 1;
    if (mouse->y < 1) mouse->y = 1;
    if (mouse->y >= VGA_HEIGHT - 1) mouse->y = VGA_HEIGHT - 2;

    mouse->buttons = report->buttons;
    vga_update_mouse_status(mouse->x, mouse->y, mouse->buttons);
}

void usb_mouse_get_state(int32_t *x, int32_t *y, uint8_t *buttons) {
    if (mouse_count > 0) {
        if (x) *x = mice[0].x;
        if (y) *y = mice[0].y;
        if (buttons) *buttons = mice[0].buttons;
    } else {
        if (x) *x = 0;
        if (y) *y = 0;
        if (buttons) *buttons = 0;
    }
}

static void submit_kbd_transfer(usb_hid_kbd_t *kbd) {
    xhci_controller_t *ctrl = xhci_get_controller();
    xhci_submit_async_trb(ctrl, kbd->dev->slot_id, kbd->in_dci, &kbd->report, sizeof(usb_kbd_report_t), true);
    kbd->transfer_pending = true;
    xhci_ring_doorbell(ctrl, kbd->dev->slot_id, kbd->in_dci);
}

static void submit_mouse_transfer(usb_hid_mouse_t *mouse) {
    xhci_controller_t *ctrl = xhci_get_controller();
    xhci_submit_async_trb(ctrl, mouse->dev->slot_id, mouse->in_dci, &mouse->report, sizeof(usb_mouse_report_t), true);
    mouse->transfer_pending = true;
    xhci_ring_doorbell(ctrl, mouse->dev->slot_id, mouse->in_dci);
}

void usb_hid_on_transfer_complete(uint8_t slot_id, uint8_t ep_dci, uint32_t status) {
    UNUSED(status);

    // Check keyboards
    for (size_t i = 0; i < kbd_count; i++) {
        if (kbds[i].active && kbds[i].dev->slot_id == slot_id && kbds[i].in_dci == ep_dci) {
            process_kbd_report(&kbds[i], &kbds[i].report);
            submit_kbd_transfer(&kbds[i]);
            return;
        }
    }

    // Check mice
    for (size_t i = 0; i < mouse_count; i++) {
        if (mice[i].active && mice[i].dev->slot_id == slot_id && mice[i].in_dci == ep_dci) {
            process_mouse_report(&mice[i], &mice[i].report);
            submit_mouse_transfer(&mice[i]);
            return;
        }
    }
}

int usb_hid_init_device(usb_device_t *dev, usb_interface_t *iface) {
    usb_control_msg(dev,
                    USB_REQ_TYPE_CLASS | USB_DIR_OUT | USB_REQ_RECIPIENT_INTERFACE,
                    USB_HID_REQ_SET_IDLE,
                    0, iface->interface_number, NULL, 0);

    usb_control_msg(dev,
                    USB_REQ_TYPE_CLASS | USB_DIR_OUT | USB_REQ_RECIPIENT_INTERFACE,
                    USB_HID_REQ_SET_PROTOCOL,
                    0, iface->interface_number, NULL, 0);

    usb_endpoint_t *in_ep = NULL;
    for (uint8_t i = 0; i < iface->num_endpoints; i++) {
        if ((iface->endpoints[i].address & USB_DIR_IN) &&
            ((iface->endpoints[i].attributes & 0x03) == 0x03)) {
            in_ep = &iface->endpoints[i];
            break;
        }
    }

    if (!in_ep) {
        kprint_color(0x4F, "[HID] No Interrupt IN endpoint found for Slot %u Iface %u\n",
                     dev->slot_id, iface->interface_number);
        return -1;
    }

    if (iface->interface_protocol == USB_HID_PROTOCOL_KEYBOARD) {
        if (kbd_count < MAX_HID_KBDS) {
            usb_hid_kbd_t *kbd = &kbds[kbd_count++];
            memset(kbd, 0, sizeof(usb_hid_kbd_t));
            kbd->dev = dev;
            kbd->in_dci = in_ep->dci;
            kbd->ep_addr = in_ep->address;
            kbd->max_packet = in_ep->max_packet_size;
            kbd->interval = in_ep->interval;
            kbd->active = true;
            snprintf(dev->name, sizeof(dev->name), "USB HID Keyboard");
            kprintf("[HID] Bound USB Keyboard to Slot %u DCI %u\n", dev->slot_id, kbd->in_dci);

            submit_kbd_transfer(kbd);
        }
    } else if (iface->interface_protocol == USB_HID_PROTOCOL_MOUSE) {
        if (mouse_count < MAX_HID_MICE) {
            usb_hid_mouse_t *m = &mice[mouse_count++];
            memset(m, 0, sizeof(usb_hid_mouse_t));
            m->dev = dev;
            m->in_dci = in_ep->dci;
            m->ep_addr = in_ep->address;
            m->max_packet = in_ep->max_packet_size;
            m->interval = in_ep->interval;
            m->x = VGA_WIDTH / 2;
            m->y = VGA_HEIGHT / 2;
            m->active = true;
            snprintf(dev->name, sizeof(dev->name), "USB HID Mouse");
            kprintf("[HID] Bound USB Mouse to Slot %u DCI %u\n", dev->slot_id, m->in_dci);
            vga_update_mouse_status(m->x, m->y, 0);

            submit_mouse_transfer(m);
        }
    }

    return 0;
}

void usb_hid_poll(void) {
    for (size_t i = 0; i < kbd_count; i++) {
        if (kbds[i].active && !kbds[i].transfer_pending) {
            submit_kbd_transfer(&kbds[i]);
        }
    }

    for (size_t i = 0; i < mouse_count; i++) {
        if (mice[i].active && !mice[i].transfer_pending) {
            submit_mouse_transfer(&mice[i]);
        }
    }
}
