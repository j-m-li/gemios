/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "xhci.h"
#include "usb_core.h"
#include "usb_hid.h"
#include "mmu.h"
#include "heap.h"
#include "vga.h"
#include "pit.h"
#include "string.h"
#include "sched.h"
#include "sync.h"
#include "io.h"

static xhci_controller_t g_xhci;
static rtos_mutex_t g_xhci_mutex;

static void xhci_lock(void) {
    rtos_mutex_lock(&g_xhci_mutex, 0xFFFFFFFF);
}

static void xhci_unlock(void) {
    rtos_mutex_unlock(&g_xhci_mutex);
}

xhci_controller_t *xhci_get_controller(void) {
    return &g_xhci;
}

static void ring_init(xhci_ring_t *ring, size_t size) {
    ring->size = size;
    ring->enqueue_idx = 0;
    ring->dequeue_idx = 0;
    ring->cycle = 1;
    ring->trbs = (xhci_trb_t*)kmalloc_aligned(size * sizeof(xhci_trb_t), 64);
    memset(ring->trbs, 0, size * sizeof(xhci_trb_t));
    ring->phys_addr = (phys_addr_t)ring->trbs;
}

static void ring_setup_link_trb(xhci_ring_t *ring) {
    xhci_trb_t *link = &ring->trbs[ring->size - 1];
    link->parameter = (uint64_t)ring->phys_addr;
    link->status = 0;
    link->control = TRB_SET_TYPE(TRB_TYPE_LINK) | TRB_TC;
}

static void xhci_ring_enqueue_trb(xhci_ring_t *ring, xhci_trb_t *trb) {
    uint32_t idx = ring->enqueue_idx;

    if (idx >= ring->size - 1) {
        ring->trbs[ring->size - 1].control = TRB_SET_TYPE(TRB_TYPE_LINK) | TRB_TC | (ring->cycle ? TRB_CYCLE : 0);
        ring->enqueue_idx = 0;
        ring->cycle ^= 1;
        idx = 0;
    }

    trb->control |= (ring->cycle ? TRB_CYCLE : 0);
    memcpy(&ring->trbs[idx], trb, sizeof(xhci_trb_t));
    ring->enqueue_idx++;
}

void xhci_ring_doorbell(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t target) {
    uintptr_t db_reg = ctrl->db_base + (slot_id * 4);
    mmio_write32(db_reg, target);
}

bool xhci_init(pci_device_t *pci_dev) {
    rtos_mutex_init(&g_xhci_mutex);
    memset(&g_xhci, 0, sizeof(xhci_controller_t));
    g_xhci.pci_dev = pci_dev;

    pci_enable_bus_mastering(pci_dev);

    uint32_t bar0 = pci_read_config32(pci_dev->bus, pci_dev->slot, pci_dev->func, 0x10);
    g_xhci.mmio_base = bar0 & ~0xF;

    kprintf("[xHCI] Initializing xHCI Host Controller (v%x.%x)\n",
            pci_dev->class_code, pci_dev->subclass);

    g_xhci.cap_base = g_xhci.mmio_base;
    g_xhci.cap_length = mmio_read8(g_xhci.cap_base + XHCI_CAP_CAPLENGTH);
    g_xhci.hci_version = mmio_read16(g_xhci.cap_base + XHCI_CAP_HCIVERSION);

    g_xhci.op_base = g_xhci.cap_base + g_xhci.cap_length;

    uint32_t rtsoff = mmio_read32(g_xhci.cap_base + XHCI_CAP_RTSOFF);
    g_xhci.rt_base = g_xhci.cap_base + (rtsoff & ~0x1F);

    uint32_t dboff = mmio_read32(g_xhci.cap_base + XHCI_CAP_DBOFF);
    g_xhci.db_base = g_xhci.cap_base + (dboff & ~0x3);

    uint32_t hcs1 = mmio_read32(g_xhci.cap_base + XHCI_CAP_HCSPARAMS1);
    g_xhci.max_slots = XHCI_HCS1_MAX_SLOTS(hcs1);
    g_xhci.max_ports = XHCI_HCS1_MAX_PORTS(hcs1);
    g_xhci.max_intrs = XHCI_HCS1_MAX_INTRS(hcs1);

    uint32_t hcc1 = mmio_read32(g_xhci.cap_base + XHCI_CAP_HCCPARAMS1);
    g_xhci.csz = XHCI_HCC1_CSZ(hcc1);

    kprintf("[xHCI] MaxSlots: %u, MaxPorts: %u, MaxIntrs: %u, CSZ: %u, MMIO: %p\n",
            g_xhci.max_slots, g_xhci.max_ports, g_xhci.max_intrs, g_xhci.csz, (void*)g_xhci.mmio_base);

    // Stop and reset controller
    uint32_t usbcmd = mmio_read32(g_xhci.op_base + XHCI_OP_USBCMD);
    usbcmd &= ~XHCI_CMD_RS;
    mmio_write32(g_xhci.op_base + XHCI_OP_USBCMD, usbcmd);

    for (int i = 0; i < 100; i++) {
        if (mmio_read32(g_xhci.op_base + XHCI_OP_USBSTS) & XHCI_STS_HCH) break;
        for (volatile int d = 0; d < 1000; d++);
    }

    mmio_write32(g_xhci.op_base + XHCI_OP_USBCMD, XHCI_CMD_HCRST);
    for (int i = 0; i < 100; i++) {
        if ((mmio_read32(g_xhci.op_base + XHCI_OP_USBCMD) & XHCI_CMD_HCRST) == 0) break;
        for (volatile int d = 0; d < 1000; d++);
    }

    // Configure Max Device Slots Enabled
    mmio_write32(g_xhci.op_base + XHCI_OP_CONFIG, g_xhci.max_slots);

    // Setup DCBAA
    size_t dcbaa_size = (g_xhci.max_slots + 1) * sizeof(uint64_t);
    g_xhci.dcbaa = (uint64_t*)kmalloc_aligned(dcbaa_size, 64);
    memset(g_xhci.dcbaa, 0, dcbaa_size);
    g_xhci.dcbaa_phys = (phys_addr_t)g_xhci.dcbaa;

    uint32_t hcs2 = mmio_read32(g_xhci.cap_base + XHCI_CAP_HCSPARAMS2);
    uint32_t max_scratch = XHCI_HCS2_MAX_SCRATCH(hcs2);
    if (max_scratch > 0) {
        uint64_t *scratch_array = (uint64_t*)kmalloc_aligned(max_scratch * sizeof(uint64_t), 64);
        for (uint32_t i = 0; i < max_scratch; i++) {
            phys_addr_t sp_page = pmm_alloc_page();
            scratch_array[i] = (uint64_t)sp_page;
        }
        g_xhci.dcbaa[0] = (uint64_t)(phys_addr_t)scratch_array;
    }

    mmio_write64(g_xhci.op_base + XHCI_OP_DCBAAP, (uint64_t)g_xhci.dcbaa_phys);

    ring_init(&g_xhci.cmd_ring, XHCI_RING_SIZE);
    ring_setup_link_trb(&g_xhci.cmd_ring);
    mmio_write64(g_xhci.op_base + XHCI_OP_CRCR, (uint64_t)g_xhci.cmd_ring.phys_addr | XHCI_CRCR_RCS);

    ring_init(&g_xhci.evt_ring, XHCI_RING_SIZE);
    g_xhci.evt_cycle = 1;

    g_xhci.erst = (xhci_erst_entry_t*)kmalloc_aligned(sizeof(xhci_erst_entry_t), 64);
    g_xhci.erst->ring_segment_base_addr = (uint64_t)g_xhci.evt_ring.phys_addr;
    g_xhci.erst->ring_segment_size = XHCI_RING_SIZE;
    g_xhci.erst->rsvd = 0;
    g_xhci.erst_phys = (phys_addr_t)g_xhci.erst;

    uintptr_t rt_intr = g_xhci.rt_base + 0x20;
    mmio_write32(rt_intr + XHCI_INTR_ERSTSZ, 1);
    mmio_write64(rt_intr + XHCI_INTR_ERDP, (uint64_t)g_xhci.evt_ring.phys_addr);
    mmio_write64(rt_intr + XHCI_INTR_ERSTBA, (uint64_t)g_xhci.erst_phys);
    mmio_write32(rt_intr + XHCI_INTR_IMOD, 0);
    mmio_write32(rt_intr + XHCI_INTR_IMAN, XHCI_IMAN_IE | XHCI_IMAN_IP);

    mmio_write32(g_xhci.op_base + XHCI_OP_USBCMD, XHCI_CMD_RS | XHCI_CMD_INTE);

    for (int i = 0; i < 100; i++) {
        if ((mmio_read32(g_xhci.op_base + XHCI_OP_USBSTS) & XHCI_STS_HCH) == 0) {
            break;
        }
        for (volatile int d = 0; d < 1000; d++);
    }

    g_xhci.initialized = true;
    kprintf("[xHCI] Host Controller started successfully.\n");

    return true;
}

static xhci_trb_t *xhci_poll_event(xhci_controller_t *ctrl) {
    xhci_trb_t *trb = &ctrl->evt_ring.trbs[ctrl->evt_ring.dequeue_idx];
    uint32_t control = trb->control;
    uint8_t cycle = control & TRB_CYCLE;

    if (cycle != ctrl->evt_cycle) {
        return NULL;
    }

    ctrl->evt_ring.dequeue_idx++;
    if (ctrl->evt_ring.dequeue_idx >= ctrl->evt_ring.size) {
        ctrl->evt_ring.dequeue_idx = 0;
        ctrl->evt_cycle ^= 1;
    }

    uintptr_t rt_intr = ctrl->rt_base + 0x20;
    phys_addr_t erdp = ctrl->evt_ring.phys_addr + (ctrl->evt_ring.dequeue_idx * sizeof(xhci_trb_t));
    mmio_write64(rt_intr + XHCI_INTR_ERDP, (uint64_t)erdp | XHCI_ERDP_EHB);

    return trb;
}

static int xhci_submit_cmd(xhci_controller_t *ctrl, xhci_trb_t *cmd_trb, xhci_trb_t *out_evt) {
    xhci_lock();
    xhci_ring_t *ring = &ctrl->cmd_ring;
    uint32_t idx = ring->enqueue_idx;

    if (idx >= ring->size - 1) {
        ring->trbs[ring->size - 1].control = TRB_SET_TYPE(TRB_TYPE_LINK) | TRB_TC | (ring->cycle ? TRB_CYCLE : 0);
        ring->enqueue_idx = 0;
        ring->cycle ^= 1;
        idx = 0;
    }

    cmd_trb->control |= (ring->cycle ? TRB_CYCLE : 0);
    memcpy((void*)&ring->trbs[idx], cmd_trb, sizeof(xhci_trb_t));

    phys_addr_t cmd_trb_phys = ring->phys_addr + (idx * sizeof(xhci_trb_t));
    ring->enqueue_idx++;

    xhci_ring_doorbell(ctrl, 0, 0);

    int result = -100;
    uint32_t start_time = pit_get_ticks();
    uint32_t loops = 0;
    while ((!rtos_is_running() || pit_get_ticks() - start_time < 1000) && ++loops < 2000000) {
        xhci_trb_t *evt = xhci_poll_event(ctrl);
        if (evt) {
            uint8_t type = TRB_GET_TYPE(evt->control);
            if (type == TRB_TYPE_CMD_COMPLETION_EVENT) {
                if (evt->parameter == (uint64_t)cmd_trb_phys) {
                    if (out_evt) {
                        memcpy(out_evt, (const void*)evt, sizeof(xhci_trb_t));
                    }
                    uint8_t code = TRB_GET_COMP_CODE(evt->status);
                    result = (code == TRB_COMP_SUCCESS) ? 0 : -(int)code;
                    break;
                }
            } else if (type == TRB_TYPE_TRANSFER_EVENT) {
                uint8_t ev_slot = TRB_GET_SLOT_ID(evt->control);
                uint8_t ev_ep = TRB_GET_EP_ID(evt->control);
                usb_hid_on_transfer_complete(ev_slot, ev_ep, evt->status);
            }
        }
    }

    xhci_unlock();
    return result;
}

int xhci_cmd_enable_slot(xhci_controller_t *ctrl, uint8_t *slot_id_out) {
    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.control = TRB_SET_TYPE(TRB_TYPE_ENABLE_SLOT);

    xhci_trb_t evt;
    int res = xhci_submit_cmd(ctrl, &trb, &evt);
    if (res == 0 && slot_id_out) {
        *slot_id_out = TRB_GET_SLOT_ID(evt.control);
    }
    return res;
}

int xhci_cmd_disable_slot(xhci_controller_t *ctrl, uint8_t slot_id) {
    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.control = TRB_SET_TYPE(TRB_TYPE_DISABLE_SLOT) | TRB_SLOT_ID(slot_id);
    return xhci_submit_cmd(ctrl, &trb, NULL);
}

int xhci_cmd_address_device(xhci_controller_t *ctrl, uint8_t slot_id, xhci_input_ctx_t *input_ctx, bool bsr) {
    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.parameter = (uint64_t)(phys_addr_t)input_ctx;
    trb.control = TRB_SET_TYPE(TRB_TYPE_ADDRESS_DEVICE) | TRB_SLOT_ID(slot_id) | (bsr ? TRB_BSR : 0);
    return xhci_submit_cmd(ctrl, &trb, NULL);
}

int xhci_cmd_configure_ep(xhci_controller_t *ctrl, uint8_t slot_id, xhci_input_ctx_t *input_ctx) {
    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.parameter = (uint64_t)(phys_addr_t)input_ctx;
    trb.control = TRB_SET_TYPE(TRB_TYPE_CONFIG_EP) | TRB_SLOT_ID(slot_id);
    return xhci_submit_cmd(ctrl, &trb, NULL);
}

int xhci_cmd_evaluate_ctx(xhci_controller_t *ctrl, uint8_t slot_id, xhci_input_ctx_t *input_ctx) {
    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.parameter = (uint64_t)(phys_addr_t)input_ctx;
    trb.control = TRB_SET_TYPE(TRB_TYPE_EVALUATE_CTX) | TRB_SLOT_ID(slot_id);
    return xhci_submit_cmd(ctrl, &trb, NULL);
}

int xhci_cmd_reset_ep(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_id) {
    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.control = TRB_SET_TYPE(TRB_TYPE_RESET_EP) | TRB_SLOT_ID(slot_id) | (((uint32_t)ep_id & 0x1F) << 16);
    return xhci_submit_cmd(ctrl, &trb, NULL);
}

void xhci_init_ep_ring(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci) {
    xhci_slot_t *slot = &ctrl->slots[slot_id];
    ring_init(&slot->ep_rings[ep_dci], XHCI_RING_SIZE);
    ring_setup_link_trb(&slot->ep_rings[ep_dci]);
}

void xhci_submit_async_trb(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci, void *data, uint32_t len, bool dir_in) {
    xhci_lock();
    xhci_slot_t *slot = &ctrl->slots[slot_id];
    xhci_ring_t *ring = &slot->ep_rings[ep_dci];

    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.parameter = (uint64_t)(phys_addr_t)data;
    trb.status = len;
    trb.control = TRB_SET_TYPE(TRB_TYPE_NORMAL) | TRB_IOC | (dir_in ? TRB_ISP : 0);

    xhci_ring_enqueue_trb(ring, &trb);
    xhci_unlock();
}

int xhci_control_transfer(xhci_controller_t *ctrl, uint8_t slot_id, usb_setup_packet_t *setup, void *data, uint16_t len) {
    xhci_lock();
    xhci_slot_t *slot = &ctrl->slots[slot_id];
    xhci_ring_t *ring = &slot->ep_rings[1];

    uint32_t trt = TRB_TRT_NONE;
    if (len > 0) {
        trt = (setup->bmRequestType & USB_DIR_IN) ? TRB_TRT_IN : TRB_TRT_OUT;
    }

    xhci_trb_t setup_trb;
    memset((void*)&setup_trb, 0, sizeof(setup_trb));
    memcpy((void*)&setup_trb.parameter, setup, sizeof(usb_setup_packet_t));
    setup_trb.status = 8;
    setup_trb.control = TRB_SET_TYPE(TRB_TYPE_SETUP_STAGE) | TRB_IDT | trt;
    xhci_ring_enqueue_trb(ring, &setup_trb);

    if (len > 0 && data) {
        xhci_trb_t data_trb;
        memset(&data_trb, 0, sizeof(data_trb));
        data_trb.parameter = (uint64_t)(phys_addr_t)data;
        data_trb.status = len;
        data_trb.control = TRB_SET_TYPE(TRB_TYPE_DATA_STAGE) | ((setup->bmRequestType & USB_DIR_IN) ? TRB_DIR_IN : TRB_DIR_OUT);
        xhci_ring_enqueue_trb(ring, &data_trb);
    }

    xhci_trb_t status_trb;
    memset(&status_trb, 0, sizeof(status_trb));
    status_trb.parameter = 0;
    status_trb.status = 0;
    status_trb.control = TRB_SET_TYPE(TRB_TYPE_STATUS_STAGE) | TRB_IOC |
                         ((trt == TRB_TRT_IN) ? TRB_DIR_OUT : TRB_DIR_IN);

    xhci_ring_enqueue_trb(ring, &status_trb);
    xhci_ring_doorbell(ctrl, slot_id, 1);

    int result = -100;
    uint32_t start_time = pit_get_ticks();
    uint32_t loops = 0;
    while ((!rtos_is_running() || pit_get_ticks() - start_time < 1000) && ++loops < 2000000) {
        xhci_trb_t *evt = xhci_poll_event(ctrl);
        if (evt) {
            uint8_t type = TRB_GET_TYPE(evt->control);
            if (type == TRB_TYPE_TRANSFER_EVENT) {
                uint8_t ev_slot = TRB_GET_SLOT_ID(evt->control);
                uint8_t ev_ep = TRB_GET_EP_ID(evt->control);
                if (ev_slot == slot_id && ev_ep == 1) {
                    uint8_t code = TRB_GET_COMP_CODE(evt->status);
                    result = (code == TRB_COMP_SUCCESS || code == TRB_COMP_SHORT_PACKET) ? 0 : -(int)code;
                    break;
                } else {
                    usb_hid_on_transfer_complete(ev_slot, ev_ep, evt->status);
                }
            }
        }
    }

    xhci_unlock();
    return result;
}

int xhci_bulk_transfer(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci, void *data, uint32_t len, bool dir_in) {
    xhci_lock();
    xhci_slot_t *slot = &ctrl->slots[slot_id];
    xhci_ring_t *ring = &slot->ep_rings[ep_dci];

    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.parameter = (uint64_t)(phys_addr_t)data;
    trb.status = len;
    trb.control = TRB_SET_TYPE(TRB_TYPE_NORMAL) | TRB_IOC | (dir_in ? TRB_ISP : 0);

    xhci_ring_enqueue_trb(ring, &trb);
    xhci_ring_doorbell(ctrl, slot_id, ep_dci);

    int result = -100;
    uint32_t start_time = pit_get_ticks();
    uint32_t bulk_loops = 0;
    while ((!rtos_is_running() || pit_get_ticks() - start_time < 1000) && ++bulk_loops < 2000000) {
        xhci_trb_t *evt = xhci_poll_event(ctrl);
        if (evt) {
            uint8_t type = TRB_GET_TYPE(evt->control);
            if (type == TRB_TYPE_TRANSFER_EVENT) {
                uint8_t ev_slot = TRB_GET_SLOT_ID(evt->control);
                uint8_t ev_ep = TRB_GET_EP_ID(evt->control);
                if (ev_slot == slot_id && ev_ep == ep_dci) {
                    uint8_t code = TRB_GET_COMP_CODE(evt->status);
                    result = (code == TRB_COMP_SUCCESS || code == TRB_COMP_SHORT_PACKET) ? (int)(len - TRB_GET_REMAINDER(evt->status)) : -(int)code;
                    break;
                } else {
                    usb_hid_on_transfer_complete(ev_slot, ev_ep, evt->status);
                }
            }
        }
    }

    xhci_unlock();
    return result;
}

int xhci_interrupt_transfer(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci, void *data, uint32_t len) {
    return xhci_bulk_transfer(ctrl, slot_id, ep_dci, data, len, true);
}

void xhci_scan_ports(xhci_controller_t *ctrl) {
    for (uint8_t port = 1; port <= ctrl->max_ports; port++) {
        uintptr_t portsc_reg = ctrl->op_base + XHCI_OP_PORTSC_BASE + ((port - 1) * 0x10);
        uint32_t portsc = mmio_read32(portsc_reg);

        if (portsc & XHCI_PORTSC_CCS) {
            kprintf("[xHCI] Port %u: Device detected. Resetting port...\n", port);

            mmio_write32(portsc_reg, (portsc & ~XHCI_PORTSC_W1C_MASK) | XHCI_PORTSC_PR);

            for (int i = 0; i < 200; i++) {
                portsc = mmio_read32(portsc_reg);
                if ((portsc & XHCI_PORTSC_PR) == 0 && (portsc & XHCI_PORTSC_PED)) {
                    break;
                }
                for (volatile int d = 0; d < 10000; d++);
            }

            mmio_write32(portsc_reg, portsc);

            uint8_t speed = XHCI_PORTSC_SPEED(portsc);
            kprintf("[xHCI] Port %u: Reset complete. Speed = %s (%u)\n",
                    port, usb_speed_to_string(speed), speed);

            uint8_t slot_id = 0;
            if (xhci_cmd_enable_slot(ctrl, &slot_id) == 0 && slot_id > 0) {
                kprintf("[xHCI] Allocated Slot ID %u for Port %u\n", slot_id, port);
                usb_enumerate_device(ctrl, slot_id, speed, port, 0, 0, 0);
            } else {
                kprint_color(0x4F, "[xHCI] Failed to enable slot for Port %u\n", port);
            }
        }
    }
}

void xhci_poll(void) {
    xhci_lock();
    xhci_trb_t *evt;
    while ((evt = xhci_poll_event(&g_xhci)) != NULL) {
        uint8_t type = TRB_GET_TYPE(evt->control);
        if (type == TRB_TYPE_TRANSFER_EVENT) {
            uint8_t ev_slot = TRB_GET_SLOT_ID(evt->control);
            uint8_t ev_ep = TRB_GET_EP_ID(evt->control);
            usb_hid_on_transfer_complete(ev_slot, ev_ep, evt->status);
        } else if (type == TRB_TYPE_PORT_STATUS_CHANGE) {
            uint8_t port_id = (evt->parameter >> 24) & 0xFF;
            kprintf("[xHCI Event] Port %u status change\n", port_id);
        }
    }
    xhci_unlock();
}
