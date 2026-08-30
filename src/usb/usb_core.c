/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "usb_core.h"
#include "usb_hub.h"
#include "usb_hid.h"
#include "usb_msc.h"
#include "usb_audio.h"
#include "heap.h"
#include "string.h"
#include "timer.h"

static usb_device_t usb_devices[USB_MAX_DEVICES];
static size_t usb_device_count = 0;

const char *usb_speed_to_string(uint8_t speed) {
    switch (speed) {
        case USB_SPEED_LOW:        return "Low-Speed (1.5 Mbps)";
        case USB_SPEED_FULL:       return "Full-Speed (12 Mbps)";
        case USB_SPEED_HIGH:       return "High-Speed (480 Mbps)";
        case USB_SPEED_SUPER:      return "SuperSpeed (5 Gbps)";
        case USB_SPEED_SUPER_PLUS: return "SuperSpeed+ (10 Gbps)";
        default:                   return "Unknown Speed";
    }
}

const char *usb_class_to_string(uint8_t class_code) {
    switch (class_code) {
        case USB_CLASS_PER_INTERFACE: return "Interface-Specific";
        case USB_CLASS_AUDIO:         return "Audio";
        case USB_CLASS_COMM:          return "Communications";
        case USB_CLASS_HID:           return "Human Interface Device (HID)";
        case USB_CLASS_MASS_STORAGE:  return "Mass Storage";
        case USB_CLASS_HUB:           return "USB Hub";
        case USB_CLASS_DATA:          return "Data";
        case USB_CLASS_VENDOR_SPEC:   return "Vendor-Specific";
        default:                      return "Other";
    }
}

void usb_core_init(void) {
    memset(usb_devices, 0, sizeof(usb_devices));
    usb_device_count = 0;
    usb_audio_init();
}

usb_device_t *usb_create_device(uint8_t slot_id, uint8_t speed, uint8_t root_port) {
    size_t i;
    for (i = 0; i < USB_MAX_DEVICES; i++) {
        if (!usb_devices[i].active) {
            memset(&usb_devices[i], 0, sizeof(usb_device_t));
            usb_devices[i].slot_id = slot_id;
            usb_devices[i].speed = speed;
            usb_devices[i].root_port = root_port;
            usb_devices[i].active = true;
            usb_device_count++;
            return &usb_devices[i];
        }
    }
    return NULL;
}

void usb_remove_device(uint8_t slot_id) {
    size_t i;
    for (i = 0; i < USB_MAX_DEVICES; i++) {
        if (usb_devices[i].active && usb_devices[i].slot_id == slot_id) {
            usb_msc_remove_device(&usb_devices[i]);
            if (usb_devices[i].raw_config_desc) {
                kfree(usb_devices[i].raw_config_desc);
            }
            usb_devices[i].active = false;
            usb_device_count--;
            break;
        }
    }
}

usb_device_t *usb_get_device_by_slot(uint8_t slot_id) {
    size_t i;
    for (i = 0; i < USB_MAX_DEVICES; i++) {
        if (usb_devices[i].active && usb_devices[i].slot_id == slot_id) {
            return &usb_devices[i];
        }
    }
    return NULL;
}

usb_device_t *usb_get_device_by_root_port(uint8_t root_port) {
    size_t i;
    for (i = 0; i < USB_MAX_DEVICES; i++) {
        if (usb_devices[i].active && usb_devices[i].root_port == root_port && usb_devices[i].parent_hub_slot == 0) {
            return &usb_devices[i];
        }
    }
    return NULL;
}

usb_device_t *usb_get_device_by_parent(uint8_t parent_hub_slot, uint8_t parent_port) {
    size_t i;
    for (i = 0; i < USB_MAX_DEVICES; i++) {
        if (usb_devices[i].active && usb_devices[i].parent_hub_slot == parent_hub_slot && usb_devices[i].parent_port == parent_port) {
            return &usb_devices[i];
        }
    }
    return NULL;
}

size_t usb_get_device_count(void) {
    return usb_device_count;
}

usb_device_t *usb_get_device_by_index(size_t index) {
    size_t count;
    size_t i;

    count = 0;
    for (i = 0; i < USB_MAX_DEVICES; i++) {
        if (usb_devices[i].active) {
            if (count == index) {
                return &usb_devices[i];
            }
            count++;
        }
    }
    return NULL;
}

int usb_control_msg(usb_device_t *dev, uint8_t req_type, uint8_t request, uint16_t value, uint16_t index, void *data, uint16_t len) {
    usb_setup_packet_t setup;
    xhci_controller_t *ctrl;

    setup.bmRequestType = req_type;
    setup.bRequest = request;
    setup.wValue = value;
    setup.wIndex = index;
    setup.wLength = len;

    ctrl = xhci_get_controller();
    return xhci_control_transfer(ctrl, dev->slot_id, &setup, data, len);
}

int usb_get_descriptor(usb_device_t *dev, uint8_t desc_type, uint8_t desc_index, void *data, uint16_t len) {
    return usb_control_msg(dev, USB_REQ_TYPE_STANDARD | USB_DIR_IN | USB_REQ_RECIPIENT_DEVICE,
                           USB_REQ_GET_DESCRIPTOR,
                           (desc_type << 8) | desc_index,
                           0, data, len);
}

int usb_set_configuration(usb_device_t *dev, uint8_t config_val) {
    return usb_control_msg(dev, USB_REQ_TYPE_STANDARD | USB_DIR_OUT | USB_REQ_RECIPIENT_DEVICE,
                           USB_REQ_SET_CONFIGURATION,
                           config_val, 0, NULL, 0);
}

int usb_clear_feature_endpoint_halt(usb_device_t *dev, uint8_t ep_addr) {
    return usb_control_msg(dev,
                           USB_REQ_TYPE_STANDARD | USB_DIR_OUT | USB_REQ_RECIPIENT_ENDPOINT,
                           USB_REQ_CLEAR_FEATURE,
                           0, /* ENDPOINT_HALT = 0 */
                           ep_addr,
                           NULL, 0);
}

static void ring_init(xhci_ring_t *ring, uint32_t size) {
    size_t bytes;
    uint32_t last;

    ring->size = size;
    ring->enqueue_idx = 0;
    ring->dequeue_idx = 0;
    ring->cycle = 1;

    bytes = size * sizeof(xhci_trb_t);
    ring->trbs = (xhci_trb_t*)kmalloc_aligned(bytes, 64);
    memset(ring->trbs, 0, bytes);
    ring->phys_addr = (phys_addr_t)ring->trbs;

    last = ring->size - 1;
    ring->trbs[last].parameter_lo = (uint32_t)ring->phys_addr;
    ring->trbs[last].parameter_hi = 0;
    ring->trbs[last].status = 0;
    ring->trbs[last].control = TRB_SET_TYPE(TRB_TYPE_LINK) | TRB_TC | (ring->cycle ? TRB_CYCLE : 0);
}

int usb_enumerate_device(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t speed, uint8_t root_port, uint8_t parent_hub_slot, uint8_t parent_port, uint32_t route_string) {
    xhci_slot_t *slot;
    void *input_ctx;
    size_t input_ctx_size;
    size_t dev_ctx_size;
    xhci_input_ctrl_ctx_t *ctrl_ctx;
    xhci_slot_ctx_t *slot_ctx;
    xhci_ep_ctx_t *ep0_ctx;
    uint32_t context_entries;
    bool is_hub;
    uint16_t ep0_max_packet;
    int res;
    usb_device_t *dev;
    usb_config_descriptor_t cfg_hdr;
    uint8_t *p;
    uint8_t *end;
    usb_interface_t *cur_iface;
    uint8_t max_dci;
    uint8_t if_idx;

    slot = &ctrl->slots[slot_id];
    memset(slot, 0, sizeof(xhci_slot_t));

    slot->enabled = true;
    slot->slot_id = slot_id;
    slot->speed = speed;
    slot->root_port = root_port;
    slot->parent_hub_slot = parent_hub_slot;
    slot->parent_port = parent_port;
    slot->route_string = route_string;

    /* 1. Allocate Device Context (32 entries * 32/64 bytes) */
    dev_ctx_size = 32 * xhci_context_size(ctrl);
    slot->dev_ctx = kmalloc_aligned(dev_ctx_size, 64);
    memset(slot->dev_ctx, 0, dev_ctx_size);
    slot->dev_ctx_phys = (phys_addr_t)slot->dev_ctx;
    ctrl->dcbaa[2 * slot_id] = (uint32_t)slot->dev_ctx_phys;
    ctrl->dcbaa[2 * slot_id + 1] = 0;

    /* 2. Allocate EP0 Transfer Ring */
    xhci_init_ep_ring(ctrl, slot_id, 1);

    /* 3. Prepare Input Context for Address Device (33 entries * 32/64 bytes) */
    input_ctx_size = 33 * xhci_context_size(ctrl);
    input_ctx = kmalloc_aligned(input_ctx_size, 64);
    memset(input_ctx, 0, input_ctx_size);

    ctrl_ctx = xhci_get_input_ctrl_ctx(ctrl, input_ctx);
    slot_ctx = xhci_get_input_slot_ctx(ctrl, input_ctx);
    ep0_ctx = xhci_get_input_ep_ctx(ctrl, input_ctx, 1);

    ctrl_ctx->add_flags = (1 << 0) | (1 << 1); /* Add Slot Context and EP0 Context */

    /* Slot Context */
    context_entries = 1;
    slot_ctx->info1 = (route_string & 0xFFFFF) | ((speed & 0x0F) << 20) | (context_entries << 27);
    slot_ctx->info2 = ((uint32_t)root_port << 16);

    /* Parent Hub Slot ID, Port, and TTT are ONLY for Low-Speed & Full-Speed devices
     * attached to a parent High-Speed Hub (Split Transactions / TT) per xHCI spec 6.2.2.1.
     * For High-Speed (480 Mbps) or SuperSpeed (5+ Gbps), info3 MUST BE 0.
     */
    if (parent_hub_slot != 0 && (speed == USB_SPEED_LOW || speed == USB_SPEED_FULL)) {
        usb_hub_t *parent_hub = usb_hub_find_by_slot(parent_hub_slot);
        if (parent_hub && parent_hub->dev && parent_hub->dev->speed == USB_SPEED_HIGH) {
            uint8_t parent_ttt = parent_hub->ttt;
            slot_ctx->info3 = (parent_hub_slot & 0xFF) | ((parent_port & 0xFF) << 8) | ((uint32_t)parent_ttt << 16);
        } else {
            slot_ctx->info3 = 0;
        }
    } else {
        slot_ctx->info3 = 0;
    }

    /* Default EP0 Max Packet Size */
    ep0_max_packet = 8;
    if (speed == USB_SPEED_FULL || speed == USB_SPEED_HIGH) {
        ep0_max_packet = 64;
    } else if (speed == USB_SPEED_SUPER || speed == USB_SPEED_SUPER_PLUS) {
        ep0_max_packet = 512;
    }

    /* EP0 Context (DCI 1) */
    ep0_ctx->info1 = (0 << 16); /* Interval = 0 */
    ep0_ctx->info2 = (3 << 1) | (4 << 3) | ((uint32_t)ep0_max_packet << 16); /* CErr=3, EP Type=4 (Control), MaxPacket */
    ep0_ctx->tr_dequeue_lo = (uint32_t)slot->ep_rings[1].phys_addr | 1; /* DCS=1 */
    ep0_ctx->tr_dequeue_hi = 0;
    ep0_ctx->tx_info = (8 & 0xFFFF); /* Average TRB length */

    dev = NULL;

    /* 4. Send Address Device command (Standard BSR=0) */
    res = xhci_cmd_address_device(ctrl, slot_id, input_ctx, false);
    if (res != 0) {
        uint8_t new_slot_id;

        /* Real hardware recovery: Reset the physical port and retry Address Device */
        kprintf("[USB] Address Device (BSR=0) failed on Slot %u (err %d), resetting port and retrying...\n", slot_id, res);

        xhci_cmd_disable_slot(ctrl, slot_id);
        ctrl->dcbaa[2 * slot_id] = 0;
        ctrl->dcbaa[2 * slot_id + 1] = 0;
        if (slot->dev_ctx) {
            kfree(slot->dev_ctx);
            slot->dev_ctx = NULL;
        }
        slot->enabled = false;

        if (parent_hub_slot == 0) {
            xhci_reset_root_port(ctrl, root_port);
        } else {
            timer_delay_ms(150);
        }

        new_slot_id = 0;
        if (xhci_cmd_enable_slot(ctrl, &new_slot_id) != 0 || new_slot_id == 0) {
            kprint_color(0x4F, "[USB] Failed to re-enable slot for Port %u\n", root_port);
            kfree(input_ctx);
            return res;
        }

        slot_id = new_slot_id;
        slot = &ctrl->slots[slot_id];
        memset(slot, 0, sizeof(xhci_slot_t));

        slot->enabled = true;
        slot->slot_id = slot_id;
        slot->speed = speed;
        slot->root_port = root_port;
        slot->parent_hub_slot = parent_hub_slot;
        slot->parent_port = parent_port;
        slot->route_string = route_string;

        slot->dev_ctx = kmalloc_aligned(dev_ctx_size, 64);
        memset(slot->dev_ctx, 0, dev_ctx_size);
        slot->dev_ctx_phys = (phys_addr_t)slot->dev_ctx;
        ctrl->dcbaa[2 * slot_id] = (uint32_t)slot->dev_ctx_phys;
        ctrl->dcbaa[2 * slot_id + 1] = 0;

        xhci_init_ep_ring(ctrl, slot_id, 1);

        memset(input_ctx, 0, input_ctx_size);
        ctrl_ctx = xhci_get_input_ctrl_ctx(ctrl, input_ctx);
        slot_ctx = xhci_get_input_slot_ctx(ctrl, input_ctx);
        ep0_ctx = xhci_get_input_ep_ctx(ctrl, input_ctx, 1);

        ctrl_ctx->add_flags = (1 << 0) | (1 << 1);
        slot_ctx->info1 = (route_string & 0xFFFFF) | ((speed & 0x0F) << 20) | (context_entries << 27);
        slot_ctx->info2 = ((uint32_t)root_port << 16);
        if (parent_hub_slot != 0 && (speed == USB_SPEED_LOW || speed == USB_SPEED_FULL)) {
            usb_hub_t *parent_hub = usb_hub_find_by_slot(parent_hub_slot);
            if (parent_hub && parent_hub->dev && parent_hub->dev->speed == USB_SPEED_HIGH) {
                uint8_t parent_ttt = parent_hub->ttt;
                slot_ctx->info3 = (parent_hub_slot & 0xFF) | ((parent_port & 0xFF) << 8) | ((uint32_t)parent_ttt << 16);
            } else {
                slot_ctx->info3 = 0;
            }
        } else {
            slot_ctx->info3 = 0;
        }

        ep0_ctx->info1 = (0 << 16);
        ep0_ctx->info2 = (3 << 1) | (4 << 3) | ((uint32_t)ep0_max_packet << 16);
        ep0_ctx->tr_dequeue_lo = (uint32_t)slot->ep_rings[1].phys_addr | 1;
        ep0_ctx->tr_dequeue_hi = 0;
        ep0_ctx->tx_info = (8 & 0xFFFF);

        timer_delay_ms(100);
        res = xhci_cmd_address_device(ctrl, slot_id, input_ctx, false);
        if (res != 0) {
            /* Try BSR=1 fallback */
            res = xhci_cmd_address_device(ctrl, slot_id, input_ctx, true);
            if (res != 0) {
                kprint_color(0x4F, "[USB] Retry Address Device failed on Slot %u (err %d)\n", slot_id, res);
                kfree(input_ctx);
                return res;
            }
        }
    }

    if (!dev) {
        dev = usb_create_device(slot_id, speed, root_port);
        if (!dev) {
            kfree(input_ctx);
            return -1;
        }
        dev->parent_hub_slot = parent_hub_slot;
        dev->parent_port = parent_port;
        dev->route_string = route_string;
        slot->usb_dev = dev;
    }

    /* 5. Get 18-byte Device Descriptor */
    res = usb_get_descriptor(dev, USB_DESC_DEVICE, 0, &dev->dev_desc, sizeof(usb_device_descriptor_t));
    if (res != 0) {
        kprint_color(0x4F, "[USB] Get Device Descriptor failed on Slot %u (err %d)\n", slot_id, res);
        kfree(input_ctx);
        return res;
    }

    kprintf("[USB] Slot %u: VID=0x%04x PID=0x%04x Class=0x%02x Sub=0x%02x Proto=0x%02x MaxPkt0=%u\n",
            slot_id, dev->dev_desc.idVendor, dev->dev_desc.idProduct,
            dev->dev_desc.bDeviceClass, dev->dev_desc.bDeviceSubClass, dev->dev_desc.bDeviceProtocol,
            dev->dev_desc.bMaxPacketSize0);

    /* If EP0 max packet size is different, update using Evaluate Context */
    {
        uint16_t real_ep0_max = dev->dev_desc.bMaxPacketSize0;
        if (speed >= USB_SPEED_SUPER) {
            /* In USB 3.0+, bMaxPacketSize0 is exponent (9 represents 2^9 = 512 bytes) */
            real_ep0_max = (1 << dev->dev_desc.bMaxPacketSize0);
        }
        if (real_ep0_max != ep0_max_packet && real_ep0_max > 0) {
            ep0_max_packet = real_ep0_max;
            memset(input_ctx, 0, input_ctx_size);
            ctrl_ctx = xhci_get_input_ctrl_ctx(ctrl, input_ctx);
            ep0_ctx = xhci_get_input_ep_ctx(ctrl, input_ctx, 1);
            ctrl_ctx->add_flags = (1 << 1); /* Add EP0 */
            ep0_ctx->info2 = (3 << 1) | (4 << 3) | ((uint32_t)ep0_max_packet << 16);
            xhci_cmd_evaluate_ctx(ctrl, slot_id, input_ctx);
        }
    }

    /* 6. Get Configuration Descriptor header to find wTotalLength */
    res = usb_get_descriptor(dev, USB_DESC_CONFIGURATION, 0, &cfg_hdr, sizeof(cfg_hdr));
    if (res != 0) {
        kprint_color(0x4F, "[USB] Get Config Descriptor header failed on Slot %u (err %d)\n", slot_id, res);
        kfree(input_ctx);
        return res;
    }

    dev->raw_config_len = cfg_hdr.wTotalLength;
    dev->raw_config_desc = (uint8_t*)kmalloc(dev->raw_config_len);
    dev->cfg_desc = cfg_hdr;

    res = usb_get_descriptor(dev, USB_DESC_CONFIGURATION, 0, dev->raw_config_desc, dev->raw_config_len);
    if (res != 0) {
        kprint_color(0x4F, "[USB] Get Full Config Descriptor failed on Slot %u (err %d)\n", slot_id, res);
        kfree(input_ctx);
        return res;
    }

    /* 7. Parse Interfaces and Endpoints */
    p = dev->raw_config_desc;
    end = p + dev->raw_config_len;
    cur_iface = NULL;
    max_dci = 1;

    memset(input_ctx, 0, input_ctx_size);
    ctrl_ctx = xhci_get_input_ctrl_ctx(ctrl, input_ctx);
    slot_ctx = xhci_get_input_slot_ctx(ctrl, input_ctx);
    ctrl_ctx->add_flags = (1 << 0); /* Slot context */

    while (p < end) {
        uint8_t len;
        uint8_t type;

        len = p[0];
        type = p[1];

        if (len == 0 || p + len > end) break;

        if (type == USB_DESC_INTERFACE) {
            usb_interface_descriptor_t *if_desc;
            if_desc = (usb_interface_descriptor_t*)p;
            if (dev->num_interfaces < USB_MAX_INTERFACES) {
                cur_iface = &dev->interfaces[dev->num_interfaces++];
                cur_iface->interface_number = if_desc->bInterfaceNumber;
                cur_iface->alternate_setting = if_desc->bAlternateSetting;
                cur_iface->interface_class = if_desc->bInterfaceClass;
                cur_iface->interface_subclass = if_desc->bInterfaceSubClass;
                cur_iface->interface_protocol = if_desc->bInterfaceProtocol;
                cur_iface->num_endpoints = 0;

                kprintf("[USB] Slot %u: Interface %u (Alt %u) Class=0x%02x Sub=0x%02x Proto=0x%02x (%s)\n",
                        slot_id, cur_iface->interface_number, cur_iface->alternate_setting,
                        cur_iface->interface_class, cur_iface->interface_subclass, cur_iface->interface_protocol,
                        usb_class_to_string(cur_iface->interface_class));
            }
        } else if (type == USB_DESC_ENDPOINT && cur_iface) {
            usb_endpoint_descriptor_t *ep_desc;
            ep_desc = (usb_endpoint_descriptor_t*)p;
            if (cur_iface->num_endpoints < USB_MAX_ENDPOINTS) {
                usb_endpoint_t *ep;
                uint8_t ep_num;
                bool dir_in;
                uint8_t dci;
                uint8_t ep_type;
                uint8_t xfer_type;
                uint8_t interval;
                uint8_t cerr;
                xhci_ep_ctx_t *ep_ctx;

                ep = &cur_iface->endpoints[cur_iface->num_endpoints++];
                ep->address = ep_desc->bEndpointAddress;
                ep->attributes = ep_desc->bmAttributes;
                ep->max_packet_size = ep_desc->wMaxPacketSize;
                ep->interval = ep_desc->bInterval;

                ep_num = ep->address & 0x0F;
                dir_in = (ep->address & USB_DIR_IN) != 0;
                dci = (ep_num * 2) + (dir_in ? 1 : 0);
                ep->dci = dci;

                if (dci > max_dci) max_dci = dci;

                /* Allocate Transfer Ring for endpoint with Link TRB setup */
                xhci_init_ep_ring(ctrl, slot_id, dci);

                /* Setup Endpoint Context in Input Context */
                ep_ctx = xhci_get_input_ep_ctx(ctrl, input_ctx, dci);
                ctrl_ctx->add_flags |= (1 << dci);

                ep_type = 0;
                xfer_type = ep->attributes & 0x03;
                if (xfer_type == 0) ep_type = 4; /* Control */
                else if (xfer_type == 1) ep_type = dir_in ? 5 : 1; /* Isoch */
                else if (xfer_type == 2) ep_type = dir_in ? 6 : 2; /* Bulk */
                else if (xfer_type == 3) ep_type = dir_in ? 7 : 3; /* Interrupt */

                uint8_t max_burst;

                interval = ep->interval;
                if (speed == USB_SPEED_FULL || speed == USB_SPEED_LOW) {
                    if (xfer_type == 1) {
                        /* For FS isochronous: interval = bInterval + 3 (1ms frame -> 8 x 125us = 2^(4-1) -> Interval = 4) */
                        interval = (ep->interval > 0) ? (ep->interval + 3) : 4;
                    } else {
                        /* In xHCI interval is encoded as 2^(interval-1)*125us */
                        /* For FS/LS interrupt, ep->interval is in 1ms frames -> log2(interval) + 3 */
                        interval = 3; /* ~1ms default */
                    }
                }

                max_burst = 0;
                if (speed >= USB_SPEED_SUPER && (p + len < end) && (p[len + 1] == USB_DESC_SS_EP_COMPANION) && (p[len] >= 3)) {
                    max_burst = p[len + 2]; /* bMaxBurst */
                }

                /* For Isochronous endpoints (Type 1 and 5), CErr must be 0 per xHCI spec 4.14.1 */
                cerr = (xfer_type == 1) ? 0 : 3;

                uint32_t max_esit = (xfer_type == 1) ? ep->max_packet_size : 0;
                ep_ctx->info1 = (interval << 16) | (((max_esit >> 16) & 0xFF) << 24);
                ep_ctx->info2 = (cerr << 1) | (ep_type << 3) | ((uint32_t)max_burst << 8) | (ep->max_packet_size << 16);
                ep_ctx->tr_dequeue_lo = (uint32_t)slot->ep_rings[dci].phys_addr | 1;
                ep_ctx->tr_dequeue_hi = 0;
                ep_ctx->tx_info = (ep->max_packet_size & 0xFFFF) | ((max_esit & 0xFFFF) << 16);

                kprintf("[USB]   Endpoint 0x%02x (DCI %u): Type=%u MaxPacket=%u Burst=%u Interval=%u\n",
                        ep->address, dci, ep_type, ep->max_packet_size, max_burst, ep->interval);
            }
        }

        p += len;
    }

    /* Update context entries in slot context */
    dev->max_dci = max_dci;
    is_hub = (dev->dev_desc.bDeviceClass == USB_CLASS_HUB) ||
             (dev->num_interfaces > 0 && dev->interfaces[0].interface_class == USB_CLASS_HUB);

    slot_ctx->info1 = (route_string & 0xFFFFF) | ((speed & 0x0F) << 20) |
                      (is_hub ? (1 << 26) : 0) | (max_dci << 27);
    slot_ctx->info2 = ((uint32_t)root_port << 16);
    if (parent_hub_slot != 0 && (speed == USB_SPEED_LOW || speed == USB_SPEED_FULL)) {
        usb_hub_t *parent_hub = usb_hub_find_by_slot(parent_hub_slot);
        if (parent_hub && parent_hub->dev && parent_hub->dev->speed == USB_SPEED_HIGH) {
            uint8_t parent_ttt = parent_hub->ttt;
            slot_ctx->info3 = (parent_hub_slot & 0xFF) | ((parent_port & 0xFF) << 8) | ((uint32_t)parent_ttt << 16);
        } else {
            slot_ctx->info3 = 0;
        }
    } else {
        slot_ctx->info3 = 0;
    }

    /* 8. Configure Endpoints */
    if (max_dci > 1) {
        kprintf("[USB] Sending Configure Endpoint on Slot %u (max_dci=%u)...\n", slot_id, max_dci);
        res = xhci_cmd_configure_ep(ctrl, slot_id, input_ctx);
        if (res != 0) {
            kprint_color(0x4F, "[USB] Configure Endpoint failed on Slot %u (err %d)\n", slot_id, res);
        } else {
            kprintf("[USB] Configure Endpoint success on Slot %u\n", slot_id);
        }
    }

    kfree(input_ctx);

    /* 9. Send Set Configuration */
    kprintf("[USB] Sending Set Configuration (%u) on Slot %u...\n", dev->cfg_desc.bConfigurationValue, slot_id);
    res = usb_set_configuration(dev, dev->cfg_desc.bConfigurationValue);
    if (res != 0) {
        kprint_color(0x4F, "[USB] Set Configuration failed on Slot %u (err %d)\n", slot_id, res);
        return res;
    }
    kprintf("[USB] Set Configuration success on Slot %u\n", slot_id);

    /* 10. Attach Drivers */
    if (is_hub) {
        snprintf(dev->name, sizeof(dev->name), "%s Hub", (speed >= USB_SPEED_SUPER) ? "USB 3.0 SuperSpeed" : "USB 2.0");
        usb_hub_init_device(dev);
    } else {
        for (if_idx = 0; if_idx < dev->num_interfaces; if_idx++) {
            usb_interface_t *iface = &dev->interfaces[if_idx];
            if (iface->interface_class == USB_CLASS_HID) {
                usb_hid_init_device(dev, iface);
            } else if (iface->interface_class == USB_CLASS_MASS_STORAGE) {
                usb_msc_init_device(dev, iface);
            } else if (iface->interface_class == USB_CLASS_AUDIO ||
                       (dev->dev_desc.idVendor == USB_VID_CMEDIA && dev->dev_desc.idProduct == USB_PID_CMEDIA_AUDIO_ADAPTER)) {
                usb_audio_init_device(dev, iface);
            }
        }
    }

    return 0;
}
