/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_XHCI_REGS_H
#define GEMIOS_XHCI_REGS_H

#include "types.h"

/* xHCI Capability Register Offsets */
#define XHCI_CAP_CAPLENGTH     0x00
#define XHCI_CAP_HCIVERSION    0x02
#define XHCI_CAP_HCSPARAMS1    0x04
#define XHCI_CAP_HCSPARAMS2    0x08
#define XHCI_CAP_HCSPARAMS3    0x0C
#define XHCI_CAP_HCCPARAMS1    0x10
#define XHCI_CAP_DBOFF         0x14
#define XHCI_CAP_RTSOFF        0x18
#define XHCI_CAP_HCCPARAMS2    0x1C

/* HCSPARAMS1 masks */
#define XHCI_HCS1_MAX_SLOTS(p)  ((p) & 0xFF)
#define XHCI_HCS1_MAX_INTRS(p)  (((p) >> 8) & 0x7FF)
#define XHCI_HCS1_MAX_PORTS(p)  (((p) >> 24) & 0xFF)

/* HCSPARAMS2 masks */
#define XHCI_HCS2_MAX_SCRATCH(p) ((((p) >> 27) & 0x1F) << 5 | (((p) >> 21) & 0x1F))

/* HCCPARAMS1 masks */
#define XHCI_HCC1_AC64(p)       ((p) & 0x01)
#define XHCI_HCC1_CSZ(p)        (((p) >> 2) & 0x01)
#define XHCI_HCC1_XECP(p)       (((p) >> 16) & 0xFFFF)

/* xHCI Operational Register Offsets (relative to OpBase) */
#define XHCI_OP_USBCMD          0x00
#define XHCI_OP_USBSTS          0x04
#define XHCI_OP_PAGESIZE        0x08
#define XHCI_OP_DNCTRL          0x14
#define XHCI_OP_CRCR            0x18 /* 64-bit */
#define XHCI_OP_DCBAAP          0x30 /* 64-bit */
#define XHCI_OP_CONFIG          0x38
#define XHCI_OP_PORTSC_BASE     0x400

/* USBCMD Bits */
#define XHCI_CMD_RS             (1 << 0)  /* Run/Stop */
#define XHCI_CMD_HCRST          (1 << 1)  /* Host Controller Reset */
#define XHCI_CMD_INTE           (1 << 2)  /* Interrupter Enable */
#define XHCI_CMD_HSEE           (1 << 3)  /* Host System Error Enable */

/* USBSTS Bits */
#define XHCI_STS_HCH            (1 << 0)  /* HC Halted */
#define XHCI_STS_HSE            (1 << 2)  /* Host System Error */
#define XHCI_STS_EINT           (1 << 3)  /* Event Interrupt */
#define XHCI_STS_PCD            (1 << 4)  /* Port Change Detect */
#define XHCI_STS_CNR            (1 << 11) /* Controller Not Ready */

/* CRCR Bits */
#define XHCI_CRCR_RCS           (1ULL << 0) /* Ring Cycle State */
#define XHCI_CRCR_CS            (1ULL << 1) /* Command Stop */
#define XHCI_CRCR_CA            (1ULL << 2) /* Command Abort */
#define XHCI_CRCR_CRR           (1ULL << 3) /* Command Ring Running */

/* PORTSC Bits */
#define XHCI_PORTSC_CCS         (1 << 0)   /* Current Connect Status */
#define XHCI_PORTSC_PED         (1 << 1)   /* Port Enabled/Disabled */
#define XHCI_PORTSC_OCA         (1 << 3)   /* Over-current Active */
#define XHCI_PORTSC_PR          (1 << 4)   /* Port Reset */
#define XHCI_PORTSC_PLS_MASK    (0xF << 5) /* Port Link State */
#define XHCI_PORTSC_PP          (1 << 9)   /* Port Power */
#define XHCI_PORTSC_SPEED(p)    (((p) >> 10) & 0x0F) /* Port Speed */
#define XHCI_PORTSC_LWS         (1 << 16)  /* Port Link State Write Strobe */
#define XHCI_PORTSC_CSC         (1 << 17)  /* Connect Status Change */
#define XHCI_PORTSC_PEC         (1 << 18)  /* Port Enable Change */
#define XHCI_PORTSC_WRC         (1 << 19)  /* Warm Port Reset Change */
#define XHCI_PORTSC_OCC         (1 << 20)  /* Over-current Change */
#define XHCI_PORTSC_PRC         (1 << 21)  /* Port Reset Change */
#define XHCI_PORTSC_PLC         (1 << 22)  /* Port Link State Change */
#define XHCI_PORTSC_CEC         (1 << 23)  /* Config Error Change */
#define XHCI_PORTSC_W1C_MASK    (XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | XHCI_PORTSC_WRC | \
                                 XHCI_PORTSC_OCC | XHCI_PORTSC_PRC | XHCI_PORTSC_PLC | \
                                 XHCI_PORTSC_CEC)

/* xHCI Runtime / Interrupter Register Offsets (relative to RtBase + 0x20*intr) */
#define XHCI_INTR_IMAN          0x00
#define XHCI_INTR_IMOD          0x04
#define XHCI_INTR_ERSTSZ        0x08
#define XHCI_INTR_ERSTBA        0x10 /* 64-bit */
#define XHCI_INTR_ERDP          0x18 /* 64-bit */

#define XHCI_IMAN_IP            (1 << 0) /* Interrupt Pending */
#define XHCI_IMAN_IE            (1 << 1) /* Interrupt Enable */
#define XHCI_ERDP_EHB           (1ULL << 3) /* Event Handler Busy */

/* Extended Capabilities */
#define XHCI_EXT_CAP_LEGSUP     0x01
#define XHCI_EXT_CAP_PROTO      0x02

#define XHCI_LEGSUP_BIOS_OWNED  (1 << 16)
#define XHCI_LEGSUP_OS_OWNED    (1 << 24)

#endif /* GEMIOS_XHCI_REGS_H */
