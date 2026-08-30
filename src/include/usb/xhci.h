/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_XHCI_H
#define GEMIOS_XHCI_H

#include "types.h"
#include "pci.h"
#include "xhci_regs.h"
#include "xhci_trb.h"
#include "usb_defs.h"

#define XHCI_RING_SIZE 4096
#define XHCI_MAX_SLOTS 32
#define XHCI_MAX_PORTS 32

typedef struct {
    xhci_trb_t *trbs;
    phys_addr_t phys_addr;
    uint32_t size;
    uint32_t enqueue_idx;
    uint32_t dequeue_idx;
    uint8_t  cycle;
} xhci_ring_t;

typedef struct xhci_slot {
    bool enabled;
    uint8_t slot_id;
    uint8_t speed;
    uint8_t root_port;
    uint8_t parent_hub_slot;
    uint8_t parent_port;
    uint32_t route_string;
    void *dev_ctx;
    phys_addr_t dev_ctx_phys;
    xhci_ring_t ep_rings[32];
    void *usb_dev;
} xhci_slot_t;

typedef struct xhci_controller {
    pci_device_t *pci_dev;
    uintptr_t mmio_base;
    uintptr_t cap_base;
    uintptr_t op_base;
    uintptr_t rt_base;
    uintptr_t db_base;

    uint8_t cap_length;
    uint16_t hci_version;
    uint8_t max_slots;
    uint8_t max_ports;
    uint16_t max_intrs;
    uint8_t csz;

    uint32_t *dcbaa;
    phys_addr_t dcbaa_phys;

    xhci_erst_entry_t *erst;
    phys_addr_t erst_phys;

    xhci_ring_t cmd_ring;
    xhci_ring_t evt_ring;
    uint8_t evt_cycle;

    xhci_slot_t slots[XHCI_MAX_SLOTS + 1];

    uint8_t irq_line;
    bool initialized;
} xhci_controller_t;

bool xhci_init(pci_device_t *pci_dev);
xhci_controller_t *xhci_get_controller(void);
void xhci_poll(void);

/* CSZ-aware context accessors */
static inline size_t xhci_context_size(xhci_controller_t *ctrl) {
    return ctrl->csz ? 64 : 32;
}

static inline xhci_input_ctrl_ctx_t *xhci_get_input_ctrl_ctx(xhci_controller_t *ctrl, void *input_ctx) {
    UNUSED(ctrl);
    return (xhci_input_ctrl_ctx_t*)input_ctx;
}

static inline xhci_slot_ctx_t *xhci_get_input_slot_ctx(xhci_controller_t *ctrl, void *input_ctx) {
    size_t sz = ctrl->csz ? 64 : 32;
    return (xhci_slot_ctx_t*)((uintptr_t)input_ctx + sz);
}

static inline xhci_ep_ctx_t *xhci_get_input_ep_ctx(xhci_controller_t *ctrl, void *input_ctx, uint8_t dci) {
    size_t sz = ctrl->csz ? 64 : 32;
    return (xhci_ep_ctx_t*)((uintptr_t)input_ctx + ((dci + 1) * sz));
}

static inline xhci_slot_ctx_t *xhci_get_dev_slot_ctx(xhci_controller_t *ctrl, void *dev_ctx) {
    UNUSED(ctrl);
    return (xhci_slot_ctx_t*)dev_ctx;
}

static inline xhci_ep_ctx_t *xhci_get_dev_ep_ctx(xhci_controller_t *ctrl, void *dev_ctx, uint8_t dci) {
    size_t sz = ctrl->csz ? 64 : 32;
    return (xhci_ep_ctx_t*)((uintptr_t)dev_ctx + (dci * sz));
}

/* xHCI Commands */
int xhci_cmd_enable_slot(xhci_controller_t *ctrl, uint8_t *slot_id_out);
int xhci_cmd_disable_slot(xhci_controller_t *ctrl, uint8_t slot_id);
int xhci_cmd_address_device(xhci_controller_t *ctrl, uint8_t slot_id, void *input_ctx, bool bsr);
int xhci_cmd_configure_ep(xhci_controller_t *ctrl, uint8_t slot_id, void *input_ctx);
int xhci_cmd_evaluate_ctx(xhci_controller_t *ctrl, uint8_t slot_id, void *input_ctx);
int xhci_cmd_reset_ep(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_id);
int xhci_cmd_set_tr_dequeue(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci, phys_addr_t tr_dequeue_ptr, uint8_t dcs);
int xhci_clear_endpoint_stall(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci, uint8_t ep_addr);

/* Ring & Doorbell helpers */
void xhci_init_ep_ring(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci);
void xhci_reset_ep_ring(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci);
void xhci_ring_doorbell(xhci_controller_t *ctrl, uint8_t target_slot, uint8_t target_dci);
void xhci_submit_async_trb(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci, void *data, uint32_t len, bool dir_in);
int xhci_control_transfer(xhci_controller_t *ctrl, uint8_t slot_id, usb_setup_packet_t *setup, void *data, uint16_t len);
int xhci_bulk_transfer(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci, void *data, uint32_t len, bool dir_in);
int xhci_interrupt_transfer(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci, void *data, uint32_t len);
int xhci_isoch_transfer(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci, void *data, uint32_t len, bool sia, bool ioc);
int xhci_isoch_transfer_frame(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci, void *data, uint32_t len, uint32_t frame_id, bool sia, bool ioc);
uint32_t xhci_get_current_frame(xhci_controller_t *ctrl);

/* Root Hub management */
void xhci_scan_ports(xhci_controller_t *ctrl);
bool xhci_reset_root_port(xhci_controller_t *ctrl, uint8_t port);

#endif /* GEMIOS_XHCI_H */
