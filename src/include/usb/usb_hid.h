/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_USB_HID_H
#define GEMIOS_USB_HID_H

#include "usb_core.h"

/* HID Boot Keyboard Report (8 bytes) */
struct usb_kbd_report {
    uint8_t modifiers; /* [0] LCtrl, [1] LShift, [2] LAlt, [3] LGUI, [4] RCtrl, [5] RShift, [6] RAlt, [7] RGUI */
    uint8_t reserved;
    uint8_t keycodes[6];
} PACKED;
typedef struct usb_kbd_report usb_kbd_report_t;

/* HID Boot Mouse Report (3 or 4 bytes) */
struct usb_mouse_report {
    uint8_t buttons; /* [0] Left, [1] Right, [2] Middle */
    int8_t  dx;
    int8_t  dy;
    int8_t  dwheel;
} PACKED;
typedef struct usb_mouse_report usb_mouse_report_t;

typedef struct usb_hid_kbd {
    usb_device_t *dev;
    uint8_t in_dci;
    uint8_t ep_addr;
    uint16_t max_packet;
    uint8_t interval;
    usb_kbd_report_t report;
    usb_kbd_report_t last_report;
    bool transfer_pending;
    bool active;
} usb_hid_kbd_t;

typedef struct usb_hid_mouse {
    usb_device_t *dev;
    uint8_t in_dci;
    uint8_t ep_addr;
    uint16_t max_packet;
    uint8_t interval;
    usb_mouse_report_t report;
    int32_t x;
    int32_t y;
    uint8_t buttons;
    bool transfer_pending;
    bool active;
} usb_hid_mouse_t;

int usb_hid_init_device(usb_device_t *dev, usb_interface_t *iface);
void usb_hid_poll(void);
void usb_hid_on_transfer_complete(uint8_t slot_id, uint8_t ep_dci, uint32_t status);

/* Shared Keyboard API */
void kbd_push_char(uint16_t key);
bool usb_kbd_has_char(void);
uint16_t usb_kbd_getchar(void);

/* Mouse interface for system */
void usb_mouse_get_state(int32_t *x, int32_t *y, uint8_t *buttons);

#endif /* GEMIOS_USB_HID_H */
