/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_USB_MSC_H
#define GEMIOS_USB_MSC_H

#include "usb_core.h"
#include "blockdev.h"

#define USB_MSC_CBW_SIGNATURE 0x43425355 /* "USBC" */
#define USB_MSC_CSW_SIGNATURE 0x53425355 /* "USBS" */

#define USB_MSC_CBW_FLAG_IN  0x80
#define USB_MSC_CBW_FLAG_OUT 0x00

#define USB_MSC_CSW_STATUS_PASSED      0x00
#define USB_MSC_CSW_STATUS_FAILED      0x01
#define USB_MSC_CSW_STATUS_PHASE_ERROR 0x02

#define USB_MSC_CBW_SIZE 31
#define USB_MSC_CSW_SIZE 13

/* Command Block Wrapper (31 bytes) */
struct usb_msc_cbw {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} PACKED;
typedef struct usb_msc_cbw usb_msc_cbw_t;

/* Command Status Wrapper (13 bytes) */
struct usb_msc_csw {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
} PACKED;
typedef struct usb_msc_csw usb_msc_csw_t;

/* SCSI Commands */
#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_REQUEST_SENSE   0x03
#define SCSI_INQUIRY         0x12
#define SCSI_READ_CAPACITY10 0x25
#define SCSI_READ10          0x28
#define SCSI_WRITE10         0x2A

typedef struct usb_msc_dev {
    usb_device_t *dev;
    uint8_t in_dci;
    uint8_t out_dci;
    uint8_t ep_in_addr;
    uint8_t ep_out_addr;
    uint16_t max_packet_in;
    uint16_t max_packet_out;
    uint8_t max_lun;

    char vendor[9];
    char product[17];
    char revision[5];

    uint32_t block_count;
    uint32_t block_size;
    bool ready;

    block_dev_t bdev;
} usb_msc_dev_t;

int usb_msc_init_device(usb_device_t *dev, usb_interface_t *iface);
int usb_msc_remove_device(usb_device_t *dev);
int usb_msc_read_blocks(usb_msc_dev_t *msc, uint32_t lba, uint32_t count, void *buf);
int usb_msc_write_blocks(usb_msc_dev_t *msc, uint32_t lba, uint32_t count, const void *buf);

size_t usb_msc_get_count(void);
usb_msc_dev_t *usb_msc_get(size_t index);

#endif /* GEMIOS_USB_MSC_H */
