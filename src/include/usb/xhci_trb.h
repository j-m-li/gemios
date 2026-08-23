/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_XHCI_TRB_H
#define GEMIOS_XHCI_TRB_H

#include "types.h"

/* TRB Types */
#define TRB_TYPE_RESERVED              0
#define TRB_TYPE_NORMAL                1
#define TRB_TYPE_SETUP_STAGE           2
#define TRB_TYPE_DATA_STAGE            3
#define TRB_TYPE_STATUS_STAGE          4
#define TRB_TYPE_ISOCH                 5
#define TRB_TYPE_LINK                  6
#define TRB_TYPE_EVENT_DATA            7
#define TRB_TYPE_NOOP                  8
#define TRB_TYPE_ENABLE_SLOT           9
#define TRB_TYPE_DISABLE_SLOT          10
#define TRB_TYPE_ADDRESS_DEVICE        11
#define TRB_TYPE_CONFIG_EP             12
#define TRB_TYPE_EVALUATE_CTX          13
#define TRB_TYPE_RESET_EP              14
#define TRB_TYPE_STOP_EP               15
#define TRB_TYPE_SET_TR_DEQUEUE        16
#define TRB_TYPE_RESET_DEVICE          17
#define TRB_TYPE_FORCE_EVENT           18
#define TRB_TYPE_NEGOTIATE_BW          19
#define TRB_TYPE_SET_LATENCY_TOL       20
#define TRB_TYPE_GET_PORT_BW           21
#define TRB_TYPE_FORCE_HEADER          22
#define TRB_TYPE_NOOP_CMD              23

#define TRB_TYPE_TRANSFER_EVENT        32
#define TRB_TYPE_CMD_COMPLETION_EVENT  33
#define TRB_TYPE_PORT_STATUS_CHANGE    34
#define TRB_TYPE_BANDWIDTH_REQ_EVENT   35
#define TRB_TYPE_DOORBELL_EVENT        36
#define TRB_TYPE_HC_EVENT              37
#define TRB_TYPE_DEV_NOTIFY_EVENT      38
#define TRB_TYPE_MFINDEX_WRAP_EVENT    39

/* Completion Codes */
#define TRB_COMP_INVALID               0
#define TRB_COMP_SUCCESS               1
#define TRB_COMP_DATA_BUFFER_ERROR     2
#define TRB_COMP_BABBLE_ERROR          3
#define TRB_COMP_USB_TRANSACTION_ERROR 4
#define TRB_COMP_TRB_ERROR             5
#define TRB_COMP_STALL_ERROR           6
#define TRB_COMP_RESOURCE_ERROR        7
#define TRB_COMP_BANDWIDTH_ERROR       8
#define TRB_COMP_NO_SLOTS_ERROR        9
#define TRB_COMP_SLOT_NOT_ENABLED      11
#define TRB_COMP_EP_NOT_ENABLED        12
#define TRB_COMP_SHORT_PACKET          13
#define TRB_COMP_RING_UNDERRUN         14
#define TRB_COMP_RING_OVERRUN          15
#define TRB_COMP_VF_EVENT_RING_FULL    16
#define TRB_COMP_PARAMETER_ERROR       17
#define TRB_COMP_CONTEXT_STATE_ERROR   19
#define TRB_COMP_EVENT_RING_FULL       21
#define TRB_COMP_INCOMPATIBLE_DEVICE   22
#define TRB_COMP_MISSED_SERVICE        23
#define TRB_COMP_CMD_RING_STOPPED      24
#define TRB_COMP_CMD_ABORTED           25
#define TRB_COMP_STOPPED               26
#define TRB_COMP_STOPPED_LENGTH_INV    27

/* TRB Flags */
#define TRB_CYCLE                      (1U << 0)
#define TRB_ENT                        (1U << 1)
#define TRB_TC                         (1U << 1) // For Link TRB: Toggle Cycle
#define TRB_ISP                        (1U << 2)
#define TRB_NS                         (1U << 3)
#define TRB_CH                         (1U << 4)
#define TRB_IOC                        (1U << 5)
#define TRB_IDT                        (1U << 6)
#define TRB_BSR                        (1U << 9) // Block Set Address Request
#define TRB_DC                         (1U << 9) // Deconfigure (for Configure EP)

#define TRB_GET_TYPE(c)                (((c) >> 10) & 0x3F)
#define TRB_SET_TYPE(t)                (((uint32_t)(t) & 0x3F) << 10)

#define TRB_TRT_NONE                   (0U << 16)
#define TRB_TRT_OUT                    (2U << 16)
#define TRB_TRT_IN                     (3U << 16)
#define TRB_DIR_IN                     (1U << 16)
#define TRB_DIR_OUT                    (0U << 16)

#define TRB_SLOT_ID(s)                 (((uint32_t)(s) & 0xFF) << 24)
#define TRB_GET_SLOT_ID(c)             (((c) >> 24) & 0xFF)
#define TRB_GET_EP_ID(c)               (((c) >> 16) & 0x1F)
#define TRB_GET_COMP_CODE(s)           (((s) >> 24) & 0xFF)
#define TRB_GET_REMAINDER(s)           ((s) & 0xFFFFFF)

/* Generic 16-byte TRB layout */
struct xhci_trb {
    volatile uint64_t parameter;
    volatile uint32_t status;
    volatile uint32_t control;
} PACKED;
typedef struct xhci_trb xhci_trb_t;

/* Event Ring Segment Table Entry (16 bytes) */
struct xhci_erst_entry {
    uint64_t ring_segment_base_addr;
    uint32_t ring_segment_size;
    uint32_t rsvd;
} PACKED;
typedef struct xhci_erst_entry xhci_erst_entry_t;

/* Slot Context (32 bytes) */
struct xhci_slot_ctx {
    uint32_t info1; // [0:19] Route String, [20:23] Speed, [24] Rsvd, [25] MTT, [26] Hub, [27:31] Context Entries
    uint32_t info2; // [0:7] Max Exit Latency, [8:15] Root Hub Port Num, [16:23] Number of Ports, [24:31] Rsvd
    uint32_t info3; // [0:7] Parent Hub Slot ID, [8:15] Parent Port Num, [16:17] TTT, [18:31] Rsvd/Interrupter Target
    uint32_t info4; // [0:7] Device Address, [8:26] Rsvd, [27:31] Slot State
    uint32_t rsvd[4];
} PACKED;
typedef struct xhci_slot_ctx xhci_slot_ctx_t;

/* Endpoint Context (32 bytes) */
struct xhci_ep_ctx {
    uint32_t info1; // [0:2] EP State, [3:7] Rsvd, [8:9] Mult, [10:14] MaxPStreams, [15] LSA, [16:23] Interval, [24:31] Max ESIT Payload Hi
    uint32_t info2; // [0] Force Event, [1:2] CErr, [3:5] EP Type, [6] Rsvd, [7] HID, [8:15] Max Burst Size, [16:31] Max Packet Size
    uint64_t tr_dequeue_ptr; // [0] DCS, [1:3] Rsvd, [4:63] TR Dequeue Pointer
    uint32_t tx_info; // [0:15] Average TRB Length, [16:31] Max ESIT Payload Lo
    uint32_t rsvd[3];
} PACKED;
typedef struct xhci_ep_ctx xhci_ep_ctx_t;

/* Input Control Context (32 bytes) */
struct xhci_input_ctrl_ctx {
    uint32_t drop_flags;
    uint32_t add_flags;
    uint32_t rsvd[6];
} PACKED;
typedef struct xhci_input_ctrl_ctx xhci_input_ctrl_ctx_t;

/* Device Context (32 * 32 bytes = 1024 bytes) */
struct xhci_device_ctx {
    xhci_slot_ctx_t slot;
    xhci_ep_ctx_t   ep[31];
} PACKED ALIGNED(64);
typedef struct xhci_device_ctx xhci_device_ctx_t;

/* Input Context (33 * 32 bytes = 1056 bytes) */
struct xhci_input_ctx {
    xhci_input_ctrl_ctx_t ctrl;
    xhci_slot_ctx_t       slot;
    xhci_ep_ctx_t         ep[31];
} PACKED ALIGNED(64);
typedef struct xhci_input_ctx xhci_input_ctx_t;

#endif /* GEMIOS_XHCI_TRB_H */
