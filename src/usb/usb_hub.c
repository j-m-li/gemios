#include "usb_hub.h"
#include "xhci.h"
#include "time.h"
#include "heap.h"
#include "string.h"
#include "pit.h"
#include "vga.h"

#define MAX_HUBS 4
static usb_hub_t hubs[MAX_HUBS];
static size_t hub_count = 0;

static int hub_get_descriptor(usb_device_t *dev, void *desc, uint16_t len) {
    uint8_t desc_type;
    desc_type = (dev->speed >= USB_SPEED_SUPER) ? USB_DESC_SS_HUB : USB_DESC_HUB;
    return usb_control_msg(dev,
                           USB_REQ_TYPE_CLASS | USB_DIR_IN | USB_REQ_RECIPIENT_DEVICE,
                           USB_REQ_GET_DESCRIPTOR,
                           (desc_type << 8) | 0,
                           0, desc, len);
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
    usb_hub_t *hub;
    int res;
    uint8_t port;
    xhci_controller_t *ctrl;
    uint8_t hub_desc_raw[32];
    void *input_ctx;

    if (hub_count >= MAX_HUBS) return -1;

    hub = &hubs[hub_count++];
    memset(hub, 0, sizeof(usb_hub_t));
    hub->dev = dev;
    hub->is_superspeed = (dev->speed >= USB_SPEED_SUPER);

    memset(hub_desc_raw, 0, sizeof(hub_desc_raw));

    if (hub->is_superspeed) {
        usb_ss_hub_descriptor_t *ss_desc = (usb_ss_hub_descriptor_t*)hub_desc_raw;
        res = hub_get_descriptor(dev, ss_desc, sizeof(usb_ss_hub_descriptor_t));
        if (res != 0) {
            /* Fallback to standard hub descriptor if needed */
            res = hub_get_descriptor(dev, ss_desc, sizeof(usb_hub_descriptor_t));
        }
        if (res != 0) {
            kprint_color(0x4F, "[HUB] Failed to get SuperSpeed Hub Descriptor on Slot %u (err %d)\n", dev->slot_id, res);
            return res;
        }
        hub->num_ports = ss_desc->bNbrPorts;
        hub->characteristics = ss_desc->wHubCharacteristics;
        hub->pwr_on_delay_ms = ss_desc->bPwrOn2PwrGood * 2;
        hub->hub_hdr_dec_lat = ss_desc->bHubHdrDecLat;
    } else {
        usb_hub_descriptor_t *usb2_desc = (usb_hub_descriptor_t*)hub_desc_raw;
        res = hub_get_descriptor(dev, usb2_desc, sizeof(usb_hub_descriptor_t));
        if (res != 0) {
            kprint_color(0x4F, "[HUB] Failed to get USB 2.0 Hub Descriptor on Slot %u (err %d)\n", dev->slot_id, res);
            return res;
        }
        hub->num_ports = usb2_desc->bNbrPorts;
        hub->characteristics = usb2_desc->wHubCharacteristics;
        hub->pwr_on_delay_ms = usb2_desc->bPwrOn2PwrGood * 2;
        hub->hub_hdr_dec_lat = 0;
    }

    if (hub->pwr_on_delay_ms == 0) hub->pwr_on_delay_ms = 100;

    kprintf("[HUB] Initialized USB Hub on Slot %u (%s): %u downstream ports, PowerDelay=%ums\n",
            dev->slot_id,
            hub->is_superspeed ? "USB 3.0 SuperSpeed" : "USB 2.0",
            hub->num_ports, hub->pwr_on_delay_ms);

    ctrl = xhci_get_controller();

    /* Update Slot Context in xHCI with NumberOfPorts and Hub latency via Evaluate Context */
    input_ctx = kmalloc_aligned(33 * xhci_context_size(ctrl), 64);
    if (input_ctx) {
        xhci_input_ctrl_ctx_t *ctrl_ctx;
        xhci_slot_ctx_t *slot_ctx;
        memset(input_ctx, 0, 33 * xhci_context_size(ctrl));
        ctrl_ctx = xhci_get_input_ctrl_ctx(ctrl, input_ctx);
        slot_ctx = xhci_get_input_slot_ctx(ctrl, input_ctx);
        ctrl_ctx->add_flags = (1 << 0); /* Slot Context */
        slot_ctx->info1 = (dev->route_string & 0xFFFFF) | ((dev->speed & 0x0F) << 20) | (1 << 26);
        slot_ctx->info2 = (dev->root_port << 16) | ((uint32_t)hub->num_ports << 24);
        if (hub->is_superspeed) {
            slot_ctx->info4 = (hub->hub_hdr_dec_lat & 0xFF);
        }
        xhci_cmd_evaluate_ctx(ctrl, dev->slot_id, input_ctx);
        kfree(input_ctx);
    }

    /* Power on all downstream ports */
    for (port = 1; port <= hub->num_ports; port++) {
        uint16_t feat = hub->is_superspeed ? USB_SS_HUB_FEAT_PORT_POWER : USB_HUB_FEAT_PORT_POWER;
        hub_set_port_feature(dev, port, feat);
    }

    /* Wait for power stabilization */
    rtos_sleep_ms(hub->pwr_on_delay_ms + 50);

    /* Check downstream ports */
    for (port = 1; port <= hub->num_ports; port++) {
        uint32_t port_status;
        uint16_t stat;

        port_status = 0;
        res = hub_get_port_status(dev, port, &port_status);
        if (res != 0) continue;

        stat = (uint16_t)(port_status & 0xFFFF);
        if (stat & (hub->is_superspeed ? USB_SS_HUB_PORT_STAT_CONNECTION : USB_HUB_PORT_STAT_CONNECTION)) {
            uint8_t speed;
            uint32_t route_string;
            uint32_t shift;
            uint8_t child_slot;

            kprintf("[HUB] Slot %u Port %u: Connected device detected. Resetting port...\n", dev->slot_id, port);

            /* Issue Port Reset */
            if (hub->is_superspeed) {
                int wait_i;
                hub_set_port_feature(dev, port, USB_SS_HUB_FEAT_PORT_RESET);
                for (wait_i = 0; wait_i < 20; wait_i++) {
                    rtos_sleep_ms(10);
                    hub_get_port_status(dev, port, &port_status);
                    stat = (uint16_t)(port_status & 0xFFFF);
                    if ((stat & USB_SS_HUB_PORT_STAT_RESET) == 0 && (stat & USB_SS_HUB_PORT_STAT_ENABLE)) {
                        break;
                    }
                }
                /* If hot reset did not enable port, try warm reset (BH_PORT_RESET) */
                if (!(stat & USB_SS_HUB_PORT_STAT_ENABLE)) {
                    hub_set_port_feature(dev, port, USB_SS_HUB_FEAT_BH_PORT_RESET);
                    for (wait_i = 0; wait_i < 20; wait_i++) {
                        rtos_sleep_ms(10);
                        hub_get_port_status(dev, port, &port_status);
                        stat = (uint16_t)(port_status & 0xFFFF);
                        if ((stat & USB_SS_HUB_PORT_STAT_RESET) == 0 && (stat & USB_SS_HUB_PORT_STAT_ENABLE)) {
                            break;
                        }
                    }
                }
                speed = USB_SPEED_SUPER;
                hub_clear_port_feature(dev, port, USB_SS_HUB_FEAT_C_PORT_RESET);
                hub_clear_port_feature(dev, port, USB_SS_HUB_FEAT_C_BH_PORT_RESET);
                hub_clear_port_feature(dev, port, USB_SS_HUB_FEAT_C_PORT_CONNECTION);
                hub_clear_port_feature(dev, port, USB_SS_HUB_FEAT_C_PORT_LINK_STATE);
            } else {
                hub_set_port_feature(dev, port, USB_HUB_FEAT_PORT_RESET);
                rtos_sleep_ms(60);

                hub_get_port_status(dev, port, &port_status);
                stat = (uint16_t)(port_status & 0xFFFF);

                speed = USB_SPEED_FULL;
                if (stat & USB_HUB_PORT_STAT_LOW_SPEED) {
                    speed = USB_SPEED_LOW;
                } else if (stat & USB_HUB_PORT_STAT_HIGH_SPEED) {
                    speed = USB_SPEED_HIGH;
                }

                hub_clear_port_feature(dev, port, USB_HUB_FEAT_C_PORT_RESET);
                hub_clear_port_feature(dev, port, USB_HUB_FEAT_C_PORT_CONNECTION);
            }

            kprintf("[HUB] Slot %u Port %u: Reset complete. Downstream Speed = %s (%u)\n",
                    dev->slot_id, port, usb_speed_to_string(speed), speed);

            /* Calculate route string for downstream device */
            route_string = dev->route_string;
            shift = 0;
            while (shift < 20 && ((route_string >> shift) & 0x0F) != 0) {
                shift += 4;
            }
            if (shift < 20) {
                route_string |= ((uint32_t)(port & 0x0F) << shift);
            }

            /* Enable slot for child device */
            child_slot = 0;
            if (xhci_cmd_enable_slot(ctrl, &child_slot) == 0 && child_slot > 0) {
                int enum_res;
                kprintf("[HUB] Allocated Slot ID %u for device on Hub Slot %u Port %u (Route=0x%x)\n",
                        child_slot, dev->slot_id, port, route_string);
                enum_res = usb_enumerate_device(ctrl, child_slot, speed, dev->root_port, dev->slot_id, port, route_string);
                if (enum_res != 0) {
                    kprint_color(0x4F, "[HUB] Enumeration failed on Hub Slot %u Port %u (err %d), disabling slot\n",
                                 dev->slot_id, port, enum_res);
                    xhci_cmd_disable_slot(ctrl, child_slot);
                }
            } else {
                kprint_color(0x4F, "[HUB] Failed to allocate slot for device on Hub Slot %u Port %u\n",
                             dev->slot_id, port);
            }
        }
    }

    return 0;
}

void usb_hub_poll(void) {
    size_t h;
    for (h = 0; h < hub_count; h++) {
        usb_hub_t *hub;
        uint8_t port;
        xhci_controller_t *ctrl;

        hub = &hubs[h];
        if (!hub->dev || !hub->dev->active) continue;

        ctrl = xhci_get_controller();

        for (port = 1; port <= hub->num_ports; port++) {
            uint32_t port_status;
            uint16_t stat;
            uint16_t change;
            int res;

            port_status = 0;
            res = hub_get_port_status(hub->dev, port, &port_status);
            if (res != 0) continue;

            stat = (uint16_t)(port_status & 0xFFFF);
            change = (uint16_t)((port_status >> 16) & 0xFFFF);

            if (change & (hub->is_superspeed ? (USB_SS_HUB_PORT_STAT_CONNECTION | (1 << 4) | (1 << 5) | (1 << 6)) : (USB_HUB_PORT_STAT_CONNECTION | (1 << 4)))) {
                if (hub->is_superspeed) {
                    hub_clear_port_feature(hub->dev, port, USB_SS_HUB_FEAT_C_PORT_CONNECTION);
                    hub_clear_port_feature(hub->dev, port, USB_SS_HUB_FEAT_C_PORT_RESET);
                    hub_clear_port_feature(hub->dev, port, USB_SS_HUB_FEAT_C_BH_PORT_RESET);
                    hub_clear_port_feature(hub->dev, port, USB_SS_HUB_FEAT_C_PORT_LINK_STATE);
                } else {
                    hub_clear_port_feature(hub->dev, port, USB_HUB_FEAT_C_PORT_CONNECTION);
                    hub_clear_port_feature(hub->dev, port, USB_HUB_FEAT_C_PORT_RESET);
                }

                if (stat & (hub->is_superspeed ? USB_SS_HUB_PORT_STAT_CONNECTION : USB_HUB_PORT_STAT_CONNECTION)) {
                    uint8_t speed;
                    uint32_t route_string;
                    uint32_t shift;
                    uint8_t child_slot;

                    if (usb_get_device_by_parent(hub->dev->slot_id, port) != NULL) {
                        continue;
                    }

                    kprintf("[HUB] Hub Slot %u Port %u: Device attached, resetting...\n", hub->dev->slot_id, port);
                    if (hub->is_superspeed) {
                        int wait_i;
                        hub_set_port_feature(hub->dev, port, USB_SS_HUB_FEAT_PORT_RESET);
                        for (wait_i = 0; wait_i < 20; wait_i++) {
                            rtos_sleep_ms(10);
                            hub_get_port_status(hub->dev, port, &port_status);
                            stat = (uint16_t)(port_status & 0xFFFF);
                            if ((stat & USB_SS_HUB_PORT_STAT_RESET) == 0 && (stat & USB_SS_HUB_PORT_STAT_ENABLE)) {
                                break;
                            }
                        }
                        if (!(stat & USB_SS_HUB_PORT_STAT_ENABLE)) {
                            hub_set_port_feature(hub->dev, port, USB_SS_HUB_FEAT_BH_PORT_RESET);
                            for (wait_i = 0; wait_i < 20; wait_i++) {
                                rtos_sleep_ms(10);
                                hub_get_port_status(hub->dev, port, &port_status);
                                stat = (uint16_t)(port_status & 0xFFFF);
                                if ((stat & USB_SS_HUB_PORT_STAT_RESET) == 0 && (stat & USB_SS_HUB_PORT_STAT_ENABLE)) {
                                    break;
                                }
                            }
                        }
                        speed = USB_SPEED_SUPER;
                        hub_clear_port_feature(hub->dev, port, USB_SS_HUB_FEAT_C_PORT_RESET);
                        hub_clear_port_feature(hub->dev, port, USB_SS_HUB_FEAT_C_BH_PORT_RESET);
                        hub_clear_port_feature(hub->dev, port, USB_SS_HUB_FEAT_C_PORT_CONNECTION);
                        hub_clear_port_feature(hub->dev, port, USB_SS_HUB_FEAT_C_PORT_LINK_STATE);
                    } else {
                        hub_set_port_feature(hub->dev, port, USB_HUB_FEAT_PORT_RESET);
                        rtos_sleep_ms(60);

                        hub_get_port_status(hub->dev, port, &port_status);
                        stat = (uint16_t)(port_status & 0xFFFF);

                        speed = USB_SPEED_FULL;
                        if (stat & USB_HUB_PORT_STAT_LOW_SPEED) speed = USB_SPEED_LOW;
                        else if (stat & USB_HUB_PORT_STAT_HIGH_SPEED) speed = USB_SPEED_HIGH;

                        hub_clear_port_feature(hub->dev, port, USB_HUB_FEAT_C_PORT_RESET);
                        hub_clear_port_feature(hub->dev, port, USB_HUB_FEAT_C_PORT_CONNECTION);
                    }

                    route_string = hub->dev->route_string;
                    shift = 0;
                    while (shift < 20 && ((route_string >> shift) & 0x0F) != 0) {
                        shift += 4;
                    }
                    if (shift < 20) {
                        route_string |= ((uint32_t)(port & 0x0F) << shift);
                    }

                    child_slot = 0;
                    if (xhci_cmd_enable_slot(ctrl, &child_slot) == 0 && child_slot > 0) {
                        int enum_res;
                        enum_res = usb_enumerate_device(ctrl, child_slot, speed, hub->dev->root_port, hub->dev->slot_id, port, route_string);
                        if (enum_res != 0) {
                            kprint_color(0x4F, "[HUB] Enumeration failed on Hub Slot %u Port %u (err %d), disabling slot\n",
                                         hub->dev->slot_id, port, enum_res);
                            xhci_cmd_disable_slot(ctrl, child_slot);
                        }
                    }
                } else {
                    /* Device disconnected from hub port */
                    usb_device_t *child = usb_get_device_by_parent(hub->dev->slot_id, port);
                    if (child) {
                        uint8_t child_slot = child->slot_id;
                        kprintf("[HUB] Hub Slot %u Port %u (Slot %u) disconnected\n",
                                hub->dev->slot_id, port, child_slot);
                        usb_remove_device(child_slot);
                        xhci_cmd_disable_slot(ctrl, child_slot);
                    }
                }
            }
        }
    }
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
