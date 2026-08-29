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
#include "timer.h"
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
    link->parameter_lo = (uint32_t)ring->phys_addr;
    link->parameter_hi = 0;
    link->status = 0;
    link->control = TRB_SET_TYPE(TRB_TYPE_LINK) | TRB_TC;
}

static void xhci_ring_enqueue_trb(xhci_ring_t *ring, xhci_trb_t *trb) {
    uint32_t idx;
    idx = ring->enqueue_idx;

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
    uintptr_t db_reg;
    db_reg = ctrl->db_base + (slot_id * 4);
    mmio_write32(db_reg, target);
}

static void xhci_bios_handoff(xhci_controller_t *ctrl, uint32_t hcc1) {
    uint32_t xecp;
    uintptr_t ext_cap_addr;

    xecp = XHCI_HCC1_XECP(hcc1);
    if (xecp == 0) return;

    ext_cap_addr = ctrl->cap_base + (xecp << 2);

    while (ext_cap_addr != 0) {
        uint32_t cap_val;
        uint8_t cap_id;
        uint8_t next_offset;

        cap_val = mmio_read32(ext_cap_addr);
        cap_id = (uint8_t)(cap_val & 0xFF);
        next_offset = (uint8_t)((cap_val >> 8) & 0xFF);

        if (cap_id == XHCI_EXT_CAP_LEGSUP) {
            if (cap_val & XHCI_LEGSUP_BIOS_OWNED) {
                int timeout;
                kprintf("[xHCI] Requesting OS ownership from BIOS (USBLEGSUP)...\n");
                mmio_write32(ext_cap_addr, cap_val | XHCI_LEGSUP_OS_OWNED);

                timeout = 1000;
                while (timeout-- > 0) {
                    cap_val = mmio_read32(ext_cap_addr);
                    if ((cap_val & XHCI_LEGSUP_BIOS_OWNED) == 0) {
                        break;
                    }
                    timer_delay_ms(1);
                }

                if (cap_val & XHCI_LEGSUP_BIOS_OWNED) {
                    kprintf("[xHCI] BIOS handoff timed out, taking ownership.\n");
                    mmio_write32(ext_cap_addr, XHCI_LEGSUP_OS_OWNED);
                } else {
                    kprintf("[xHCI] BIOS handoff successful.\n");
                }
            }

            /* Disable BIOS SMIs in USBLEGCTLSTS (offset + 4) */
            mmio_write32(ext_cap_addr + 4, 0);
            break;
        }

        if (next_offset == 0) break;
        ext_cap_addr += ((uintptr_t)next_offset << 2);
    }
}

bool xhci_init(pci_device_t *pci_dev) {
    uint32_t bar0;
    uint32_t rtsoff;
    uint32_t dboff;
    uint32_t hcs1;
    uint32_t hcc1;
    uint32_t usbcmd;
    size_t dcbaa_size;
    uint32_t hcs2;
    uint32_t max_scratch;
    uintptr_t rt_intr;
    int i;
    volatile int d;
    uint8_t p_idx;

    rtos_mutex_init(&g_xhci_mutex);
    memset(&g_xhci, 0, sizeof(xhci_controller_t));
    g_xhci.pci_dev = pci_dev;

    pci_enable_bus_mastering(pci_dev);

    bar0 = pci_read_config32(pci_dev->bus, pci_dev->slot, pci_dev->func, 0x10);
    g_xhci.mmio_base = bar0 & ~0xF;

    kprintf("[xHCI] Initializing xHCI Host Controller (v%x.%x)\n",
            pci_dev->class_code, pci_dev->subclass);

    g_xhci.cap_base = g_xhci.mmio_base;
    g_xhci.cap_length = mmio_read8(g_xhci.cap_base + XHCI_CAP_CAPLENGTH);
    g_xhci.hci_version = mmio_read16(g_xhci.cap_base + XHCI_CAP_HCIVERSION);

    g_xhci.op_base = g_xhci.cap_base + g_xhci.cap_length;

    rtsoff = mmio_read32(g_xhci.cap_base + XHCI_CAP_RTSOFF);
    g_xhci.rt_base = g_xhci.cap_base + (rtsoff & ~0x1F);

    dboff = mmio_read32(g_xhci.cap_base + XHCI_CAP_DBOFF);
    g_xhci.db_base = g_xhci.cap_base + (dboff & ~0x3);

    hcs1 = mmio_read32(g_xhci.cap_base + XHCI_CAP_HCSPARAMS1);
    g_xhci.max_slots = XHCI_HCS1_MAX_SLOTS(hcs1);
    g_xhci.max_ports = XHCI_HCS1_MAX_PORTS(hcs1);
    g_xhci.max_intrs = XHCI_HCS1_MAX_INTRS(hcs1);

    hcc1 = mmio_read32(g_xhci.cap_base + XHCI_CAP_HCCPARAMS1);
    g_xhci.csz = XHCI_HCC1_CSZ(hcc1);

    kprintf("[xHCI] MaxSlots: %u, MaxPorts: %u, MaxIntrs: %u, CSZ: %u, MMIO: %p\n",
            g_xhci.max_slots, g_xhci.max_ports, g_xhci.max_intrs, g_xhci.csz, (void*)g_xhci.mmio_base);

    /* 1. Perform BIOS-to-OS handoff */
    xhci_bios_handoff(&g_xhci, hcc1);

    /* 2. Stop and reset controller */
    usbcmd = mmio_read32(g_xhci.op_base + XHCI_OP_USBCMD);
    usbcmd &= ~XHCI_CMD_RS;
    mmio_write32(g_xhci.op_base + XHCI_OP_USBCMD, usbcmd);

    for (i = 0; i < 100; i++) {
        if (mmio_read32(g_xhci.op_base + XHCI_OP_USBSTS) & XHCI_STS_HCH) break;
        for (d = 0; d < 1000; d++);
    }

    mmio_write32(g_xhci.op_base + XHCI_OP_USBCMD, XHCI_CMD_HCRST);
    for (i = 0; i < 100; i++) {
        if ((mmio_read32(g_xhci.op_base + XHCI_OP_USBCMD) & XHCI_CMD_HCRST) == 0) break;
        for (d = 0; d < 1000; d++);
    }

    /* Wait for Controller Not Ready (CNR) to clear */
    for (i = 0; i < 200; i++) {
        if ((mmio_read32(g_xhci.op_base + XHCI_OP_USBSTS) & XHCI_STS_CNR) == 0) break;
        for (d = 0; d < 1000; d++);
    }

    /* Configure Max Device Slots Enabled */
    mmio_write32(g_xhci.op_base + XHCI_OP_CONFIG, g_xhci.max_slots);

    /* Setup DCBAA */
    dcbaa_size = (g_xhci.max_slots + 1) * 2 * sizeof(uint32_t);
    g_xhci.dcbaa = (uint32_t*)kmalloc_aligned(dcbaa_size, 64);
    memset(g_xhci.dcbaa, 0, dcbaa_size);
    g_xhci.dcbaa_phys = (phys_addr_t)g_xhci.dcbaa;

    hcs2 = mmio_read32(g_xhci.cap_base + XHCI_CAP_HCSPARAMS2);
    max_scratch = XHCI_HCS2_MAX_SCRATCH(hcs2);
    if (max_scratch > 0) {
        uint32_t *scratch_array;
        uint32_t sc;
        scratch_array = (uint32_t*)kmalloc_aligned(max_scratch * 2 * sizeof(uint32_t), 64);
        memset(scratch_array, 0, max_scratch * 2 * sizeof(uint32_t));
        for (sc = 0; sc < max_scratch; sc++) {
            phys_addr_t sp_page;
            sp_page = pmm_alloc_page();
            scratch_array[2 * sc] = (uint32_t)sp_page;
            scratch_array[2 * sc + 1] = 0;
        }
        g_xhci.dcbaa[0] = (uint32_t)(phys_addr_t)scratch_array;
        g_xhci.dcbaa[1] = 0;
    }

    mmio_write64(g_xhci.op_base + XHCI_OP_DCBAAP, (uint32_t)g_xhci.dcbaa_phys, 0);

    ring_init(&g_xhci.cmd_ring, XHCI_RING_SIZE);
    ring_setup_link_trb(&g_xhci.cmd_ring);
    mmio_write64(g_xhci.op_base + XHCI_OP_CRCR, (uint32_t)g_xhci.cmd_ring.phys_addr | XHCI_CRCR_RCS, 0);

    ring_init(&g_xhci.evt_ring, XHCI_RING_SIZE);
    g_xhci.evt_cycle = 1;

    g_xhci.erst = (xhci_erst_entry_t*)kmalloc_aligned(sizeof(xhci_erst_entry_t), 64);
    g_xhci.erst->ring_segment_base_lo = (uint32_t)g_xhci.evt_ring.phys_addr;
    g_xhci.erst->ring_segment_base_hi = 0;
    g_xhci.erst->ring_segment_size = XHCI_RING_SIZE;
    g_xhci.erst->rsvd = 0;
    g_xhci.erst_phys = (phys_addr_t)g_xhci.erst;

    rt_intr = g_xhci.rt_base + 0x20;
    mmio_write32(rt_intr + XHCI_INTR_ERSTSZ, 1);
    mmio_write64(rt_intr + XHCI_INTR_ERDP, (uint32_t)g_xhci.evt_ring.phys_addr, 0);
    mmio_write64(rt_intr + XHCI_INTR_ERSTBA, (uint32_t)g_xhci.erst_phys, 0);
    mmio_write32(rt_intr + XHCI_INTR_IMOD, 0);
    mmio_write32(rt_intr + XHCI_INTR_IMAN, XHCI_IMAN_IE | XHCI_IMAN_IP);

    mmio_write32(g_xhci.op_base + XHCI_OP_USBCMD, XHCI_CMD_RS | XHCI_CMD_INTE);

    for (i = 0; i < 100; i++) {
        if ((mmio_read32(g_xhci.op_base + XHCI_OP_USBSTS) & XHCI_STS_HCH) == 0) {
            break;
        }
        for (d = 0; d < 1000; d++);
    }

    /* 3. Power on all Root Hub Ports */
    for (p_idx = 1; p_idx <= g_xhci.max_ports; p_idx++) {
        uintptr_t portsc_reg;
        uint32_t portsc;

        portsc_reg = g_xhci.op_base + XHCI_OP_PORTSC_BASE + ((p_idx - 1) * 0x10);
        portsc = mmio_read32(portsc_reg);
        if (!(portsc & XHCI_PORTSC_PP)) {
            mmio_write32(portsc_reg, (portsc & XHCI_PORTSC_PRESERVE_MASK) | XHCI_PORTSC_PP);
        }
    }
    timer_delay_ms(100); /* 100ms connection debounce & stabilization (USB TATTDB) */

    g_xhci.initialized = true;
    kprintf("[xHCI] Host Controller started successfully.\n");

    return true;
}

static xhci_trb_t *xhci_poll_event(xhci_controller_t *ctrl) {
    xhci_trb_t *trb;
    uint32_t control;
    uint8_t cycle;
    uintptr_t rt_intr;
    phys_addr_t erdp;

    trb = &ctrl->evt_ring.trbs[ctrl->evt_ring.dequeue_idx];
    control = trb->control;
    cycle = control & TRB_CYCLE;

    if (cycle != ctrl->evt_cycle) {
        return NULL;
    }

    ctrl->evt_ring.dequeue_idx++;
    if (ctrl->evt_ring.dequeue_idx >= ctrl->evt_ring.size) {
        ctrl->evt_ring.dequeue_idx = 0;
        ctrl->evt_cycle ^= 1;
    }

    rt_intr = ctrl->rt_base + 0x20;
    erdp = ctrl->evt_ring.phys_addr + (ctrl->evt_ring.dequeue_idx * sizeof(xhci_trb_t));
    mmio_write64(rt_intr + XHCI_INTR_ERDP, (uint32_t)erdp | XHCI_ERDP_EHB, 0);

    return trb;
}

static int xhci_submit_cmd(xhci_controller_t *ctrl, xhci_trb_t *cmd_trb, xhci_trb_t *out_evt) {
    xhci_ring_t *ring;
    uint32_t idx;
    phys_addr_t cmd_trb_phys;
    int result;
    uint32_t start_time;
    uint32_t loops;

    xhci_lock();
    ring = &ctrl->cmd_ring;
    idx = ring->enqueue_idx;

    if (idx >= ring->size - 1) {
        ring->trbs[ring->size - 1].control = TRB_SET_TYPE(TRB_TYPE_LINK) | TRB_TC | (ring->cycle ? TRB_CYCLE : 0);
        ring->enqueue_idx = 0;
        ring->cycle ^= 1;
        idx = 0;
    }

    cmd_trb->control |= (ring->cycle ? TRB_CYCLE : 0);
    memcpy((void*)&ring->trbs[idx], cmd_trb, sizeof(xhci_trb_t));

    cmd_trb_phys = ring->phys_addr + (idx * sizeof(xhci_trb_t));
    ring->enqueue_idx++;

    xhci_ring_doorbell(ctrl, 0, 0);

    result = -100;
    start_time = pit_get_ticks();
    loops = 0;
    while ((!rtos_is_running() || pit_get_ticks() - start_time < 1000) && ++loops < 2000000) {
        xhci_trb_t *evt;
        evt = xhci_poll_event(ctrl);
        if (evt) {
            uint8_t type;
            type = TRB_GET_TYPE(evt->control);
            if (type == TRB_TYPE_CMD_COMPLETION_EVENT) {
                if (evt->parameter_lo == (uint32_t)cmd_trb_phys) {
                    uint8_t code;
                    if (out_evt) {
                        memcpy(out_evt, (const void*)evt, sizeof(xhci_trb_t));
                    }
                    code = TRB_GET_COMP_CODE(evt->status);
                    result = (code == TRB_COMP_SUCCESS) ? 0 : -(int)code;
                    break;
                }
            } else if (type == TRB_TYPE_TRANSFER_EVENT) {
                uint8_t ev_slot;
                uint8_t ev_ep;
                ev_slot = TRB_GET_SLOT_ID(evt->control);
                ev_ep = TRB_GET_EP_ID(evt->control);
                usb_hid_on_transfer_complete(ev_slot, ev_ep, evt->status);
            }
        }
    }

    xhci_unlock();
    return result;
}

int xhci_cmd_enable_slot(xhci_controller_t *ctrl, uint8_t *slot_id_out) {
    xhci_trb_t trb;
    xhci_trb_t evt;
    int res;

    memset(&trb, 0, sizeof(trb));
    trb.control = TRB_SET_TYPE(TRB_TYPE_ENABLE_SLOT);

    res = xhci_submit_cmd(ctrl, &trb, &evt);
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

int xhci_cmd_address_device(xhci_controller_t *ctrl, uint8_t slot_id, void *input_ctx, bool bsr) {
    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.parameter_lo = (uint32_t)(phys_addr_t)input_ctx;
    trb.parameter_hi = 0;
    trb.control = TRB_SET_TYPE(TRB_TYPE_ADDRESS_DEVICE) | TRB_SLOT_ID(slot_id) | (bsr ? TRB_BSR : 0);
    return xhci_submit_cmd(ctrl, &trb, NULL);
}

int xhci_cmd_configure_ep(xhci_controller_t *ctrl, uint8_t slot_id, void *input_ctx) {
    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.parameter_lo = (uint32_t)(phys_addr_t)input_ctx;
    trb.parameter_hi = 0;
    trb.control = TRB_SET_TYPE(TRB_TYPE_CONFIG_EP) | TRB_SLOT_ID(slot_id);
    return xhci_submit_cmd(ctrl, &trb, NULL);
}

int xhci_cmd_evaluate_ctx(xhci_controller_t *ctrl, uint8_t slot_id, void *input_ctx) {
    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.parameter_lo = (uint32_t)(phys_addr_t)input_ctx;
    trb.parameter_hi = 0;
    trb.control = TRB_SET_TYPE(TRB_TYPE_EVALUATE_CTX) | TRB_SLOT_ID(slot_id);
    return xhci_submit_cmd(ctrl, &trb, NULL);
}

int xhci_cmd_reset_ep(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_id) {
    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.control = TRB_SET_TYPE(TRB_TYPE_RESET_EP) | TRB_SLOT_ID(slot_id) | (((uint32_t)ep_id & 0x1F) << 16);
    return xhci_submit_cmd(ctrl, &trb, NULL);
}

int xhci_cmd_set_tr_dequeue(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci, phys_addr_t tr_dequeue_ptr, uint8_t dcs) {
    xhci_trb_t trb;
    memset(&trb, 0, sizeof(trb));
    trb.parameter_lo = (uint32_t)tr_dequeue_ptr | (dcs & 0x01);
    trb.parameter_hi = 0;
    trb.control = TRB_SET_TYPE(TRB_TYPE_SET_TR_DEQUEUE) | TRB_SLOT_ID(slot_id) | (((uint32_t)ep_dci & 0x1F) << 16);
    return xhci_submit_cmd(ctrl, &trb, NULL);
}

int xhci_clear_endpoint_stall(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci, uint8_t ep_addr) {
    xhci_slot_t *slot;
    xhci_ring_t *ring;
    phys_addr_t dequeue_ptr;

    if (slot_id > XHCI_MAX_SLOTS || ep_dci >= 32) return -1;

    slot = &ctrl->slots[slot_id];
    ring = &slot->ep_rings[ep_dci];

    /* 1. Reset xHCI Endpoint (moves EP from Halted to Stopped) */
    xhci_cmd_reset_ep(ctrl, slot_id, ep_dci);

    /* 2. Update TR Dequeue Pointer to current ring position */
    dequeue_ptr = ring->phys_addr + (ring->dequeue_idx * sizeof(xhci_trb_t));
    xhci_cmd_set_tr_dequeue(ctrl, slot_id, ep_dci, dequeue_ptr, ring->cycle);

    /* 3. Send Clear Feature (ENDPOINT_HALT) to USB device */
    if (slot->usb_dev) {
        usb_clear_feature_endpoint_halt((usb_device_t*)slot->usb_dev, ep_addr);
    }
    return 0;
}

void xhci_init_ep_ring(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci) {
    xhci_slot_t *slot;
    slot = &ctrl->slots[slot_id];
    ring_init(&slot->ep_rings[ep_dci], XHCI_RING_SIZE);
    ring_setup_link_trb(&slot->ep_rings[ep_dci]);
}

void xhci_submit_async_trb(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci, void *data, uint32_t len, bool dir_in) {
    xhci_slot_t *slot;
    xhci_ring_t *ring;
    xhci_trb_t trb;

    xhci_lock();
    slot = &ctrl->slots[slot_id];
    ring = &slot->ep_rings[ep_dci];

    memset(&trb, 0, sizeof(trb));
    trb.parameter_lo = (uint32_t)(phys_addr_t)data;
    trb.parameter_hi = 0;
    trb.status = len;
    trb.control = TRB_SET_TYPE(TRB_TYPE_NORMAL) | TRB_IOC | (dir_in ? TRB_ISP : 0);

    xhci_ring_enqueue_trb(ring, &trb);
    xhci_unlock();
}

int xhci_control_transfer(xhci_controller_t *ctrl, uint8_t slot_id, usb_setup_packet_t *setup, void *data, uint16_t len) {
    xhci_slot_t *slot;
    xhci_ring_t *ring;
    uint32_t trt;
    xhci_trb_t setup_trb;
    xhci_trb_t status_trb;
    int result;
    uint32_t start_time;
    uint32_t loops;

    xhci_lock();
    slot = &ctrl->slots[slot_id];
    ring = &slot->ep_rings[1];

    trt = TRB_TRT_NONE;
    if (len > 0) {
        trt = (setup->bmRequestType & USB_DIR_IN) ? TRB_TRT_IN : TRB_TRT_OUT;
    }

    memset((void*)&setup_trb, 0, sizeof(setup_trb));
    memcpy((void*)&setup_trb.parameter_lo, setup, sizeof(usb_setup_packet_t));
    setup_trb.status = 8;
    setup_trb.control = TRB_SET_TYPE(TRB_TYPE_SETUP_STAGE) | TRB_IDT | trt;
    xhci_ring_enqueue_trb(ring, &setup_trb);

    if (len > 0 && data) {
        xhci_trb_t data_trb;
        memset(&data_trb, 0, sizeof(data_trb));
        data_trb.parameter_lo = (uint32_t)(phys_addr_t)data;
        data_trb.parameter_hi = 0;
        data_trb.status = len;
        data_trb.control = TRB_SET_TYPE(TRB_TYPE_DATA_STAGE) | ((setup->bmRequestType & USB_DIR_IN) ? TRB_DIR_IN : TRB_DIR_OUT);
        xhci_ring_enqueue_trb(ring, &data_trb);
    }

    memset(&status_trb, 0, sizeof(status_trb));
    status_trb.parameter_lo = 0;
    status_trb.parameter_hi = 0;
    status_trb.status = 0;
    status_trb.control = TRB_SET_TYPE(TRB_TYPE_STATUS_STAGE) | TRB_IOC |
                         ((trt == TRB_TRT_IN) ? TRB_DIR_OUT : TRB_DIR_IN);

    xhci_ring_enqueue_trb(ring, &status_trb);
    xhci_ring_doorbell(ctrl, slot_id, 1);

    result = -100;
    start_time = pit_get_ticks();
    loops = 0;
    while ((!rtos_is_running() || pit_get_ticks() - start_time < 1000) && ++loops < 2000000) {
        xhci_trb_t *evt;
        evt = xhci_poll_event(ctrl);
        if (evt) {
            uint8_t type;
            type = TRB_GET_TYPE(evt->control);
            if (type == TRB_TYPE_TRANSFER_EVENT) {
                uint8_t ev_slot;
                uint8_t ev_ep;
                ev_slot = TRB_GET_SLOT_ID(evt->control);
                ev_ep = TRB_GET_EP_ID(evt->control);
                if (ev_slot == slot_id && ev_ep == 1) {
                    uint8_t code;
                    code = TRB_GET_COMP_CODE(evt->status);
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
    xhci_slot_t *slot;
    xhci_ring_t *ring;
    xhci_trb_t trb;
    int result;
    uint32_t start_time;
    uint32_t bulk_loops;

    xhci_lock();
    slot = &ctrl->slots[slot_id];
    ring = &slot->ep_rings[ep_dci];

    memset(&trb, 0, sizeof(trb));
    trb.parameter_lo = (uint32_t)(phys_addr_t)data;
    trb.parameter_hi = 0;
    trb.status = len;
    trb.control = TRB_SET_TYPE(TRB_TYPE_NORMAL) | TRB_IOC | (dir_in ? TRB_ISP : 0);

    xhci_ring_enqueue_trb(ring, &trb);
    xhci_ring_doorbell(ctrl, slot_id, ep_dci);

    result = -100;
    start_time = pit_get_ticks();
    bulk_loops = 0;
    while ((!rtos_is_running() || pit_get_ticks() - start_time < 1000) && ++bulk_loops < 2000000) {
        xhci_trb_t *evt;
        evt = xhci_poll_event(ctrl);
        if (evt) {
            uint8_t type;
            type = TRB_GET_TYPE(evt->control);
            if (type == TRB_TYPE_TRANSFER_EVENT) {
                uint8_t ev_slot;
                uint8_t ev_ep;
                ev_slot = TRB_GET_SLOT_ID(evt->control);
                ev_ep = TRB_GET_EP_ID(evt->control);
                if (ev_slot == slot_id && ev_ep == ep_dci) {
                    uint8_t code;
                    code = TRB_GET_COMP_CODE(evt->status);
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
    uint8_t port;
    for (port = 1; port <= ctrl->max_ports; port++) {
        uintptr_t portsc_reg;
        uint32_t portsc;
        uint8_t speed;
        uint8_t slot_id;
        int i;

        portsc_reg = ctrl->op_base + XHCI_OP_PORTSC_BASE + ((port - 1) * 0x10);
        portsc = mmio_read32(portsc_reg);

        /* Ensure port is powered */
        if (!(portsc & XHCI_PORTSC_PP)) {
            mmio_write32(portsc_reg, (portsc & XHCI_PORTSC_PRESERVE_MASK) | XHCI_PORTSC_PP);
            timer_delay_ms(20);
            portsc = mmio_read32(portsc_reg);
        }

        if (!(portsc & XHCI_PORTSC_CCS)) {
            continue;
        }

        /* Skip if a device on this root port is already enumerated */
        if (usb_get_device_by_root_port(port) != NULL) {
            continue;
        }

        kprintf("[xHCI] Port %u: Device detected (PORTSC=0x%08x)\n", port, portsc);

        /* If port is not enabled, perform Port Reset */
        if (!(portsc & XHCI_PORTSC_PED)) {
            uint32_t reset_cmd;
            bool reset_ok;

            reset_cmd = (portsc & XHCI_PORTSC_PRESERVE_MASK) | XHCI_PORTSC_PR;
            mmio_write32(portsc_reg, reset_cmd);

            reset_ok = false;
            for (i = 0; i < 300; i++) {
                timer_delay_ms(1);
                portsc = mmio_read32(portsc_reg);
                if ((portsc & XHCI_PORTSC_PR) == 0 && (portsc & XHCI_PORTSC_PED)) {
                    reset_ok = true;
                    break;
                }
            }

            /* If normal reset did not enable port, try Warm Port Reset (for SuperSpeed) */
            if (!reset_ok && (portsc & XHCI_PORTSC_CCS)) {
                mmio_write32(portsc_reg, (portsc & XHCI_PORTSC_PRESERVE_MASK) | XHCI_PORTSC_WPR);
                for (i = 0; i < 300; i++) {
                    timer_delay_ms(1);
                    portsc = mmio_read32(portsc_reg);
                    if ((portsc & XHCI_PORTSC_WPR) == 0 && (portsc & XHCI_PORTSC_PED)) {
                        reset_ok = true;
                        break;
                    }
                }
            }

            /* Clear change bits (W1C) */
            mmio_write32(portsc_reg, (portsc & XHCI_PORTSC_PRESERVE_MASK) | (portsc & XHCI_PORTSC_W1C_MASK));
            timer_delay_ms(150); /* Port Reset Recovery delay (150ms for real hardware USB 2.0 drives) */
            portsc = mmio_read32(portsc_reg);
        }

        if (!(portsc & XHCI_PORTSC_CCS) || !(portsc & XHCI_PORTSC_PED)) {
            kprintf("[xHCI] Port %u: Port not enabled after reset (PORTSC=0x%08x)\n", port, portsc);
            continue;
        }

        speed = XHCI_PORTSC_SPEED(portsc);
        kprintf("[xHCI] Port %u: Connected. Speed = %s (%u)\n",
                port, usb_speed_to_string(speed), speed);

        slot_id = 0;
        if (xhci_cmd_enable_slot(ctrl, &slot_id) == 0 && slot_id > 0) {
            int enum_res;
            kprintf("[xHCI] Allocated Slot ID %u for Port %u\n", slot_id, port);
            enum_res = usb_enumerate_device(ctrl, slot_id, speed, port, 0, 0, 0);
            if (enum_res != 0) {
                kprint_color(0x4F, "[xHCI] Enumeration failed on Port %u Slot %u (err %d), disabling slot\n",
                             port, slot_id, enum_res);
                xhci_cmd_disable_slot(ctrl, slot_id);
            }
        } else {
            kprint_color(0x4F, "[xHCI] Failed to enable slot for Port %u\n", port);
        }
    }
}

void xhci_poll(void) {
    xhci_trb_t *evt;
    xhci_lock();
    while ((evt = xhci_poll_event(&g_xhci)) != NULL) {
        uint8_t type;
        type = TRB_GET_TYPE(evt->control);
        if (type == TRB_TYPE_TRANSFER_EVENT) {
            uint8_t ev_slot;
            uint8_t ev_ep;
            ev_slot = TRB_GET_SLOT_ID(evt->control);
            ev_ep = TRB_GET_EP_ID(evt->control);
            usb_hid_on_transfer_complete(ev_slot, ev_ep, evt->status);
        } else if (type == TRB_TYPE_PORT_STATUS_CHANGE) {
            uint8_t port_id;
            port_id = (evt->parameter_lo >> 24) & 0xFF;
            if (port_id >= 1 && port_id <= g_xhci.max_ports) {
                uintptr_t portsc_reg;
                uint32_t portsc;
                portsc_reg = g_xhci.op_base + XHCI_OP_PORTSC_BASE + ((port_id - 1) * 0x10);
                portsc = mmio_read32(portsc_reg);
                /* Clear change bits */
                mmio_write32(portsc_reg, (portsc & XHCI_PORTSC_PRESERVE_MASK) | (portsc & XHCI_PORTSC_W1C_MASK));

                if (portsc & XHCI_PORTSC_CCS) {
                    if (usb_get_device_by_root_port(port_id) == NULL) {
                        kprintf("[xHCI Event] Port %u device connected, scanning...\n", port_id);
                        xhci_unlock();
                        timer_delay_ms(150); /* 150ms connection debounce & stabilization (USB TATTDB) */
                        xhci_scan_ports(&g_xhci);
                        xhci_lock();
                    }
                } else {
                    usb_device_t *dev = usb_get_device_by_root_port(port_id);
                    if (dev && dev->parent_hub_slot == 0) {
                        uint8_t s_id = dev->slot_id;
                        kprintf("[xHCI Event] Port %u (Slot %u) disconnected\n", port_id, s_id);
                        usb_remove_device(s_id);
                        xhci_cmd_disable_slot(&g_xhci, s_id);
                    }
                }
            }
        }
    }
    xhci_unlock();
}
