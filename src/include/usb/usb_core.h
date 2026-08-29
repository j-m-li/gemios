/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_USB_CORE_H
#define GEMIOS_USB_CORE_H

#include "types.h"
#include "usb_defs.h"
#include "xhci.h"

#define USB_MAX_DEVICES 32
#define USB_MAX_ENDPOINTS 16
#define USB_MAX_INTERFACES 8

typedef struct usb_endpoint {
    uint8_t address;          /* Endpoint Address (e.g. 0x81, 0x02) */
    uint8_t attributes;       /* Transfer type */
    uint16_t max_packet_size; /* Max packet size */
    uint8_t interval;         /* Polling interval */
    uint8_t dci;              /* Device Context Index (1..31) */
} usb_endpoint_t;

typedef struct usb_interface {
    uint8_t interface_number;
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint8_t num_endpoints;
    usb_endpoint_t endpoints[USB_MAX_ENDPOINTS];
    void *driver_data;
} usb_interface_t;

typedef struct usb_device {
    uint8_t slot_id;
    uint8_t speed;
    uint8_t address;
    uint8_t root_port;
    uint8_t parent_hub_slot;
    uint8_t parent_port;
    uint32_t route_string;

    usb_device_descriptor_t dev_desc;
    usb_config_descriptor_t cfg_desc;
    uint8_t *raw_config_desc;
    uint16_t raw_config_len;

    uint8_t num_interfaces;
    usb_interface_t interfaces[USB_MAX_INTERFACES];

    char name[48];
    bool active;
} usb_device_t;

void usb_core_init(void);
usb_device_t *usb_create_device(uint8_t slot_id, uint8_t speed, uint8_t root_port);
int usb_enumerate_device(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t speed, uint8_t root_port, uint8_t parent_hub_slot, uint8_t parent_port, uint32_t route_string);
void usb_remove_device(uint8_t slot_id);

usb_device_t *usb_get_device_by_slot(uint8_t slot_id);
usb_device_t *usb_get_device_by_root_port(uint8_t root_port);
usb_device_t *usb_get_device_by_parent(uint8_t parent_hub_slot, uint8_t parent_port);
size_t usb_get_device_count(void);
usb_device_t *usb_get_device_by_index(size_t index);

int usb_control_msg(usb_device_t *dev, uint8_t req_type, uint8_t request, uint16_t value, uint16_t index, void *data, uint16_t len);
int usb_get_descriptor(usb_device_t *dev, uint8_t desc_type, uint8_t desc_index, void *data, uint16_t len);
int usb_set_configuration(usb_device_t *dev, uint8_t config_val);
int usb_clear_feature_endpoint_halt(usb_device_t *dev, uint8_t ep_addr);

#endif /* GEMIOS_USB_CORE_H */
