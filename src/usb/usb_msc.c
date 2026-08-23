/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "usb_msc.h"
#include "xhci.h"
#include "blockdev.h"
#include "heap.h"
#include "string.h"

#define MAX_MSC_DEVS 4
static usb_msc_dev_t msc_devs[MAX_MSC_DEVS];
static size_t msc_count = 0;
static uint32_t msc_tag = 0x12345678;

static inline uint32_t be32(uint32_t val) {
    return ((val >> 24) & 0xFF) |
           ((val >> 8) & 0xFF00) |
           ((val << 8) & 0xFF0000) |
           ((val << 24) & 0xFF000000);
}

static int msc_bot_transfer(usb_msc_dev_t *msc, const void *cdb, uint8_t cdb_len,
                            void *data, uint32_t data_len, bool dir_in) {
    xhci_controller_t *ctrl = xhci_get_controller();
    uint32_t tag = ++msc_tag;

    // 1. Prepare Command Block Wrapper (CBW)
    usb_msc_cbw_t cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature = USB_MSC_CBW_SIGNATURE;
    cbw.dCBWTag = tag;
    cbw.dCBWDataTransferLength = data_len;
    cbw.bmCBWFlags = dir_in ? USB_MSC_CBW_FLAG_IN : USB_MSC_CBW_FLAG_OUT;
    cbw.bCBWLUN = 0;
    cbw.bCBWCBLength = cdb_len;
    memcpy(cbw.CBWCB, cdb, cdb_len);

    // Send CBW via Bulk OUT
    int res = xhci_bulk_transfer(ctrl, msc->dev->slot_id, msc->out_dci, &cbw, sizeof(cbw), false);
    if (res < 0) {
        kprint_color(0x4F, "[MSC] Failed to send CBW (err %d)\n", res);
        return res;
    }

    // 2. Data Stage
    if (data_len > 0 && data) {
        uint8_t dci = dir_in ? msc->in_dci : msc->out_dci;
        res = xhci_bulk_transfer(ctrl, msc->dev->slot_id, dci, data, data_len, dir_in);
        if (res < 0) {
            kprint_color(0x4F, "[MSC] Data stage failed (err %d)\n", res);
            return res;
        }
    }

    // 3. Receive Command Status Wrapper (CSW) via Bulk IN
    usb_msc_csw_t csw;
    memset(&csw, 0, sizeof(csw));
    res = xhci_bulk_transfer(ctrl, msc->dev->slot_id, msc->in_dci, &csw, sizeof(csw), true);
    if (res < 0) {
        kprint_color(0x4F, "[MSC] Failed to receive CSW (err %d)\n", res);
        return res;
    }

    if (csw.dCSWSignature != USB_MSC_CSW_SIGNATURE || csw.dCSWTag != tag) {
        kprint_color(0x4F, "[MSC] CSW Signature/Tag mismatch! (sig=0x%x, tag=0x%x)\n",
                     csw.dCSWSignature, csw.dCSWTag);
        return -1;
    }

    return (csw.bCSWStatus == USB_MSC_CSW_STATUS_PASSED) ? 0 : -(int)csw.bCSWStatus;
}

static int scsi_inquiry(usb_msc_dev_t *msc) {
    uint8_t cdb[6] = { SCSI_INQUIRY, 0, 0, 0, 36, 0 };
    uint8_t buffer[36];
    memset(buffer, 0, sizeof(buffer));

    int res = msc_bot_transfer(msc, cdb, sizeof(cdb), buffer, sizeof(buffer), true);
    if (res == 0) {
        memcpy(msc->vendor, &buffer[8], 8);
        msc->vendor[8] = '\0';
        memcpy(msc->product, &buffer[16], 16);
        msc->product[16] = '\0';
        memcpy(msc->revision, &buffer[32], 4);
        msc->revision[4] = '\0';

        // Trim trailing spaces
        for (int i = 7; i >= 0 && msc->vendor[i] == ' '; i--) msc->vendor[i] = '\0';
        for (int i = 15; i >= 0 && msc->product[i] == ' '; i--) msc->product[i] = '\0';
        for (int i = 3; i >= 0 && msc->revision[i] == ' '; i--) msc->revision[i] = '\0';
    }
    return res;
}

static int scsi_test_unit_ready(usb_msc_dev_t *msc) {
    uint8_t cdb[6] = { SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0 };
    return msc_bot_transfer(msc, cdb, sizeof(cdb), NULL, 0, false);
}

static int scsi_read_capacity(usb_msc_dev_t *msc) {
    uint8_t cdb[10] = { SCSI_READ_CAPACITY10, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint32_t buffer[2];
    memset(buffer, 0, sizeof(buffer));

    int res = msc_bot_transfer(msc, cdb, sizeof(cdb), buffer, sizeof(buffer), true);
    if (res == 0) {
        uint32_t last_lba = be32(buffer[0]);
        uint32_t block_size = be32(buffer[1]);

        msc->block_count = last_lba + 1;
        msc->block_size = block_size;
    }
    return res;
}

int usb_msc_read_blocks(usb_msc_dev_t *msc, uint32_t lba, uint32_t count, void *buf) {
    if (!msc || !msc->ready || count == 0) return -1;

    uint8_t cdb[10];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_READ10;
    cdb[2] = (uint8_t)((lba >> 24) & 0xFF);
    cdb[3] = (uint8_t)((lba >> 16) & 0xFF);
    cdb[4] = (uint8_t)((lba >> 8) & 0xFF);
    cdb[5] = (uint8_t)(lba & 0xFF);
    cdb[7] = (uint8_t)((count >> 8) & 0xFF);
    cdb[8] = (uint8_t)(count & 0xFF);

    uint32_t bytes = count * msc->block_size;
    return msc_bot_transfer(msc, cdb, sizeof(cdb), buf, bytes, true);
}

int usb_msc_write_blocks(usb_msc_dev_t *msc, uint32_t lba, uint32_t count, const void *buf) {
    if (!msc || !msc->ready || count == 0) return -1;

    uint8_t cdb[10];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_WRITE10;
    cdb[2] = (uint8_t)((lba >> 24) & 0xFF);
    cdb[3] = (uint8_t)((lba >> 16) & 0xFF);
    cdb[4] = (uint8_t)((lba >> 8) & 0xFF);
    cdb[5] = (uint8_t)(lba & 0xFF);
    cdb[7] = (uint8_t)((count >> 8) & 0xFF);
    cdb[8] = (uint8_t)(count & 0xFF);

    uint32_t bytes = count * msc->block_size;
    return msc_bot_transfer(msc, cdb, sizeof(cdb), (void*)buf, bytes, false);
}

static int bdev_read_wrapper(block_dev_t *bdev, uint32_t lba, uint32_t count, void *buf) {
    usb_msc_dev_t *msc = (usb_msc_dev_t*)bdev->priv;
    return usb_msc_read_blocks(msc, lba, count, buf);
}

static int bdev_write_wrapper(block_dev_t *bdev, uint32_t lba, uint32_t count, const void *buf) {
    usb_msc_dev_t *msc = (usb_msc_dev_t*)bdev->priv;
    return usb_msc_write_blocks(msc, lba, count, buf);
}

int usb_msc_init_device(usb_device_t *dev, usb_interface_t *iface) {
    if (msc_count >= MAX_MSC_DEVS) return -1;

    usb_endpoint_t *in_ep = NULL;
    usb_endpoint_t *out_ep = NULL;

    for (uint8_t i = 0; i < iface->num_endpoints; i++) {
        uint8_t type = iface->endpoints[i].attributes & 0x03;
        if (type == 0x02) { // Bulk endpoint
            if (iface->endpoints[i].address & USB_DIR_IN) {
                in_ep = &iface->endpoints[i];
            } else {
                out_ep = &iface->endpoints[i];
            }
        }
    }

    if (!in_ep || !out_ep) {
        kprint_color(0x4F, "[MSC] Missing Bulk IN or Bulk OUT endpoints on Slot %u\n", dev->slot_id);
        return -1;
    }

    usb_msc_dev_t *msc = &msc_devs[msc_count];
    memset(msc, 0, sizeof(usb_msc_dev_t));

    msc->dev = dev;
    msc->in_dci = in_ep->dci;
    msc->out_dci = out_ep->dci;
    msc->ep_in_addr = in_ep->address;
    msc->ep_out_addr = out_ep->address;
    msc->max_packet_in = in_ep->max_packet_size;
    msc->max_packet_out = out_ep->max_packet_size;

    // Execute SCSI Inquiry
    if (scsi_inquiry(msc) != 0) {
        kprint_color(0x4F, "[MSC] SCSI INQUIRY failed on Slot %u\n", dev->slot_id);
        return -1;
    }

    // Check Test Unit Ready
    scsi_test_unit_ready(msc);

    // Read Capacity
    if (scsi_read_capacity(msc) != 0) {
        kprint_color(0x4F, "[MSC] SCSI READ CAPACITY failed on Slot %u\n", dev->slot_id);
        return -1;
    }

    msc->ready = true;
    snprintf(dev->name, sizeof(dev->name), "%s %s", msc->vendor, msc->product);

    kprintf("[MSC] USB Mass Storage initialized: '%s' '%s' (Rev %s)\n",
            msc->vendor, msc->product, msc->revision);
    kprintf("[MSC] Capacity: %u blocks of %u bytes (%u MB)\n",
            msc->block_count, msc->block_size,
            (uint32_t)(((uint64_t)msc->block_count * msc->block_size) / (1024 * 1024)));

    // Register block device
    snprintf(msc->bdev.name, sizeof(msc->bdev.name), "usb%u", (uint32_t)msc_count);
    msc->bdev.block_size = msc->block_size;
    msc->bdev.total_blocks = msc->block_count;
    msc->bdev.read = bdev_read_wrapper;
    msc->bdev.write = bdev_write_wrapper;
    msc->bdev.priv = msc;

    blockdev_register(&msc->bdev);
    msc_count++;

    return 0;
}

size_t usb_msc_get_count(void) {
    return msc_count;
}

usb_msc_dev_t *usb_msc_get(size_t index) {
    if (index < msc_count) {
        return &msc_devs[index];
    }
    return NULL;
}
