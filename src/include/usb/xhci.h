/*
 * This is free and unencumbered software released into the public domain.
 * GEMOS Preemptive Real-Time Operating System
 */

#ifndef GEMOS_XHCI_H
#define GEMOS_XHCI_H

#include "types.h"
#include "pci.h"
#include "xhci_regs.h"
#include "xhci_trb.h"
#include "usb_defs.h"

#define XHCI_RING_SIZE 64
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
    xhci_device_ctx_t *dev_ctx;
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

    uint64_t *dcbaa;
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

/* xHCI Commands */
int xhci_cmd_enable_slot(xhci_controller_t *ctrl, uint8_t *slot_id_out);
int xhci_cmd_disable_slot(xhci_controller_t *ctrl, uint8_t slot_id);
int xhci_cmd_address_device(xhci_controller_t *ctrl, uint8_t slot_id, xhci_input_ctx_t *input_ctx, bool bsr);
int xhci_cmd_configure_ep(xhci_controller_t *ctrl, uint8_t slot_id, xhci_input_ctx_t *input_ctx);
int xhci_cmd_evaluate_ctx(xhci_controller_t *ctrl, uint8_t slot_id, xhci_input_ctx_t *input_ctx);
int xhci_cmd_reset_ep(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_id);

/* Ring & Doorbell helpers */
void xhci_ring_doorbell(xhci_controller_t *ctrl, uint8_t target_slot, uint8_t target_dci);
void xhci_submit_async_trb(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci, void *data, uint32_t len, bool dir_in);
int xhci_control_transfer(xhci_controller_t *ctrl, uint8_t slot_id, usb_setup_packet_t *setup, void *data, uint16_t len);
int xhci_bulk_transfer(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci, void *data, uint32_t len, bool dir_in);
int xhci_interrupt_transfer(xhci_controller_t *ctrl, uint8_t slot_id, uint8_t ep_dci, void *data, uint32_t len);

/* Root Hub management */
void xhci_scan_ports(xhci_controller_t *ctrl);

#endif /* GEMOS_XHCI_H */
