/*
 * This is free and unencumbered software released into the public domain.
 * GEMOS Preemptive Real-Time Operating System
 */

#include "usb_hub.h"
#include "xhci.h"
#include "time.h"
#include "heap.h"
#include "string.h"

#define MAX_HUBS 4
static usb_hub_t hubs[MAX_HUBS];
static size_t hub_count = 0;

static int hub_get_descriptor(usb_device_t *dev, usb_hub_descriptor_t *desc) {
    return usb_control_msg(dev,
                           USB_REQ_TYPE_CLASS | USB_DIR_IN | USB_REQ_RECIPIENT_DEVICE,
                           USB_REQ_GET_DESCRIPTOR,
                           (USB_DESC_HUB << 8) | 0,
                           0, desc, sizeof(usb_hub_descriptor_t));
}

static int hub_set_port_feature(usb_device_t *dev, uint8_t port, uint16_t feature) {
    return usb_control_msg(dev,
                           USB_REQ_TYPE_CLASS | USB_DIR_OUT | USB_REQ_RECIPIENT_OTHER,
                           USB_REQ_SET_FEATURE,
                           feature, port, NULL, 0);
}

static int hub_clear_port_feature(usb_device_t *dev, uint8_t port, uint16_t feature) {
    return usb_control_msg(dev,
                           USB_REQ_TYPE_CLASS | USB_DIR_OUT | USB_REQ_RECIPIENT_OTHER,
                           USB_REQ_CLEAR_FEATURE,
                           feature, port, NULL, 0);
}

static int hub_get_port_status(usb_device_t *dev, uint8_t port, uint32_t *status) {
    return usb_control_msg(dev,
                           USB_REQ_TYPE_CLASS | USB_DIR_IN | USB_REQ_RECIPIENT_OTHER,
                           USB_REQ_GET_STATUS,
                           0, port, status, sizeof(uint32_t));
}

int usb_hub_init_device(usb_device_t *dev) {
    if (hub_count >= MAX_HUBS) return -1;

    usb_hub_t *hub = &hubs[hub_count++];
    memset(hub, 0, sizeof(usb_hub_t));
    hub->dev = dev;

    usb_hub_descriptor_t hub_desc;
    memset(&hub_desc, 0, sizeof(hub_desc));

    int res = hub_get_descriptor(dev, &hub_desc);
    if (res != 0) {
        kprint_color(0x4F, "[HUB] Failed to get Hub Descriptor on Slot %u (err %d)\n", dev->slot_id, res);
        return res;
    }

    hub->num_ports = hub_desc.bNbrPorts;
    hub->characteristics = hub_desc.wHubCharacteristics;
    hub->pwr_on_delay_ms = hub_desc.bPwrOn2PwrGood * 2;
    if (hub->pwr_on_delay_ms == 0) hub->pwr_on_delay_ms = 100;

    kprintf("[HUB] Initialized USB Hub on Slot %u: %u downstream ports, PowerDelay=%ums\n",
            dev->slot_id, hub->num_ports, hub->pwr_on_delay_ms);

    // Power on all downstream ports
    for (uint8_t port = 1; port <= hub->num_ports; port++) {
        hub_set_port_feature(dev, port, USB_HUB_FEAT_PORT_POWER);
    }

    // Wait for power stabilization
    rtos_sleep_ms(hub->pwr_on_delay_ms + 50);

    xhci_controller_t *ctrl = xhci_get_controller();

    // Check downstream ports
    for (uint8_t port = 1; port <= hub->num_ports; port++) {
        uint32_t port_status = 0;
        res = hub_get_port_status(dev, port, &port_status);
        if (res != 0) continue;

        uint16_t stat = (uint16_t)(port_status & 0xFFFF);
        if (stat & USB_HUB_PORT_STAT_CONNECTION) {
            kprintf("[HUB] Slot %u Port %u: Connected device detected. Resetting port...\n", dev->slot_id, port);

            // Issue Port Reset
            hub_set_port_feature(dev, port, USB_HUB_FEAT_PORT_RESET);
            rtos_sleep_ms(60);

            // Read status again
            res = hub_get_port_status(dev, port, &port_status);
            stat = (uint16_t)(port_status & 0xFFFF);

            uint8_t speed = USB_SPEED_FULL;
            if (stat & USB_HUB_PORT_STAT_LOW_SPEED) {
                speed = USB_SPEED_LOW;
            } else if (stat & USB_HUB_PORT_STAT_HIGH_SPEED) {
                speed = USB_SPEED_HIGH;
            }

            kprintf("[HUB] Slot %u Port %u: Reset complete. Downstream Speed = %s (%u)\n",
                    dev->slot_id, port, usb_speed_to_string(speed), speed);

            // Clear port change feature
            hub_clear_port_feature(dev, port, USB_HUB_FEAT_C_PORT_RESET);
            hub_clear_port_feature(dev, port, USB_HUB_FEAT_C_PORT_CONNECTION);

            // Calculate route string for downstream device
            // xHCI Route String is 20 bits: 4 bits per hub level (up to 5 levels)
            uint32_t route_string = dev->route_string;
            uint32_t shift = 0;
            while (shift < 20 && ((route_string >> shift) & 0x0F) != 0) {
                shift += 4;
            }
            if (shift < 20) {
                route_string |= ((uint32_t)(port & 0x0F) << shift);
            }

            // Enable slot for child device
            uint8_t child_slot = 0;
            if (xhci_cmd_enable_slot(ctrl, &child_slot) == 0 && child_slot > 0) {
                kprintf("[HUB] Allocated Slot ID %u for device on Hub Slot %u Port %u (Route=0x%x)\n",
                        child_slot, dev->slot_id, port, route_string);
                usb_enumerate_device(ctrl, child_slot, speed, dev->root_port, dev->slot_id, port, route_string);
            } else {
                kprint_color(0x4F, "[HUB] Failed to allocate slot for device on Hub Slot %u Port %u\n",
                             dev->slot_id, port);
            }
        }
    }

    return 0;
}

void usb_hub_poll(void) {
    // Background polling for hub port hot-plugging if needed
}

size_t usb_hub_get_count(void) {
    return hub_count;
}

usb_hub_t *usb_hub_get(size_t index) {
    if (index < hub_count) {
        return &hubs[index];
    }
    return NULL;
}
