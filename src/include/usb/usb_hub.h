/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_USB_HUB_H
#define GEMIOS_USB_HUB_H

#include "usb_core.h"

#define MAX_HUB_PORTS 16

typedef struct usb_hub {
    usb_device_t *dev;
    uint8_t num_ports;
    uint16_t characteristics;
    uint8_t pwr_on_delay_ms;
    bool is_superspeed;
    uint8_t hub_hdr_dec_lat;
    uint8_t ttt;
    uint8_t port_status[MAX_HUB_PORTS];
    bool port_connected[MAX_HUB_PORTS];
} usb_hub_t;

int usb_hub_init_device(usb_device_t *dev);
void usb_hub_poll(void);
size_t usb_hub_get_count(void);
usb_hub_t *usb_hub_get(size_t index);
usb_hub_t *usb_hub_find_by_slot(uint8_t slot_id);

#endif /* GEMIOS_USB_HUB_H */
