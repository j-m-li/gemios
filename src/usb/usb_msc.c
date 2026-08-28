/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "usb_msc.h"
#include "xhci.h"
#include "blockdev.h"
#include "heap.h"
#include "string.h"
#include "pit.h"
#include "vga.h"

#define MAX_MSC_DEVS 4
static usb_msc_dev_t msc_devs[MAX_MSC_DEVS];
static size_t msc_count = 0;
static uint32_t msc_tag = 0x12345678;

static uint32_t be32(uint32_t val) {
    return ((val >> 24) & 0xFF) |
           ((val >> 8) & 0xFF00) |
           ((val << 8) & 0xFF0000) |
           ((val << 24) & 0xFF000000);
}

static int msc_get_max_lun(usb_msc_dev_t *msc) {
    uint8_t max_lun;
    int res;

    max_lun = 0;
    res = usb_control_msg(msc->dev,
                          USB_REQ_TYPE_CLASS | USB_DIR_IN | USB_REQ_RECIPIENT_INTERFACE,
                          0xFE, /* GET_MAX_LUN */
                          0, 0, &max_lun, 1);
    if (res == 0) {
        msc->max_lun = max_lun;
    } else {
        msc->max_lun = 0;
        /* If GET_MAX_LUN STALLed, clear EP0 stall */
        usb_clear_feature_endpoint_halt(msc->dev, 0);
    }
    return 0;
}

static int msc_bot_transfer(usb_msc_dev_t *msc, const void *cdb, uint8_t cdb_len,
                            void *data, uint32_t data_len, bool dir_in) {
    xhci_controller_t *ctrl;
    uint32_t tag;
    uint8_t cbw[USB_MSC_CBW_SIZE];
    uint8_t csw[USB_MSC_CSW_SIZE];
    uint32_t csw_sig;
    uint32_t csw_tag;
    uint8_t csw_status;
    int res;

    ctrl = xhci_get_controller();
    tag = ++msc_tag;

    /* 1. Command Block Wrapper (CBW - 31 bytes) */
    memset(cbw, 0, USB_MSC_CBW_SIZE);
    cbw[0] = 0x55;
    cbw[1] = 0x53;
    cbw[2] = 0x42;
    cbw[3] = 0x43; /* "USBC" */
    cbw[4] = (uint8_t)(tag & 0xFF);
    cbw[5] = (uint8_t)((tag >> 8) & 0xFF);
    cbw[6] = (uint8_t)((tag >> 16) & 0xFF);
    cbw[7] = (uint8_t)((tag >> 24) & 0xFF);
    cbw[8] = (uint8_t)(data_len & 0xFF);
    cbw[9] = (uint8_t)((data_len >> 8) & 0xFF);
    cbw[10] = (uint8_t)((data_len >> 16) & 0xFF);
    cbw[11] = (uint8_t)((data_len >> 24) & 0xFF);
    cbw[12] = dir_in ? USB_MSC_CBW_FLAG_IN : USB_MSC_CBW_FLAG_OUT;
    cbw[13] = 0; /* LUN */
    cbw[14] = cdb_len;
    memcpy(&cbw[15], cdb, cdb_len);

    res = xhci_bulk_transfer(ctrl, msc->dev->slot_id, msc->out_dci, cbw, USB_MSC_CBW_SIZE, false);
    if (res < 0) {
        xhci_clear_endpoint_stall(ctrl, msc->dev->slot_id, msc->out_dci, msc->ep_out_addr);
        return res;
    }

    /* 2. Data Stage */
    if (data_len > 0 && data) {
        uint8_t dci;
        uint8_t ep_addr;
        dci = dir_in ? msc->in_dci : msc->out_dci;
        ep_addr = dir_in ? msc->ep_in_addr : msc->ep_out_addr;
        res = xhci_bulk_transfer(ctrl, msc->dev->slot_id, dci, data, data_len, dir_in);
        if (res < 0) {
            xhci_clear_endpoint_stall(ctrl, msc->dev->slot_id, dci, ep_addr);
        }
    }

    /* 3. Command Status Wrapper (CSW - 13 bytes) */
    memset(csw, 0, USB_MSC_CSW_SIZE);
    res = xhci_bulk_transfer(ctrl, msc->dev->slot_id, msc->in_dci, csw, USB_MSC_CSW_SIZE, true);
    if (res < 0) {
        /* Clear IN stall and retry reading CSW */
        xhci_clear_endpoint_stall(ctrl, msc->dev->slot_id, msc->in_dci, msc->ep_in_addr);
        memset(csw, 0, USB_MSC_CSW_SIZE);
        res = xhci_bulk_transfer(ctrl, msc->dev->slot_id, msc->in_dci, csw, USB_MSC_CSW_SIZE, true);
        if (res < 0) {
            return res;
        }
    }

    csw_sig = (uint32_t)csw[0] | ((uint32_t)csw[1] << 8) | ((uint32_t)csw[2] << 16) | ((uint32_t)csw[3] << 24);
    csw_tag = (uint32_t)csw[4] | ((uint32_t)csw[5] << 8) | ((uint32_t)csw[6] << 16) | ((uint32_t)csw[7] << 24);
    csw_status = csw[12];

    if (csw_sig != USB_MSC_CSW_SIGNATURE || csw_tag != tag) {
        return -1;
    }

    return (csw_status == USB_MSC_CSW_STATUS_PASSED) ? 0 : -(int)csw_status;
}

static int scsi_request_sense(usb_msc_dev_t *msc, uint8_t *sense_key, uint8_t *asc, uint8_t *ascq) {
    uint8_t cdb[6];
    uint8_t buffer[18];
    int res;

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_REQUEST_SENSE;
    cdb[4] = sizeof(buffer); /* 18 bytes */

    memset(buffer, 0, sizeof(buffer));
    res = msc_bot_transfer(msc, cdb, sizeof(cdb), buffer, sizeof(buffer), true);
    if (res == 0) {
        if (sense_key) *sense_key = buffer[2] & 0x0F;
        if (asc) *asc = buffer[12];
        if (ascq) *ascq = buffer[13];
    }
    return res;
}

static int scsi_inquiry(usb_msc_dev_t *msc) {
    uint8_t cdb[6];
    uint8_t buffer[36];
    int res;
    int i;

    cdb[0] = SCSI_INQUIRY;
    cdb[1] = 0;
    cdb[2] = 0;
    cdb[3] = 0;
    cdb[4] = 36;
    cdb[5] = 0;

    memset(buffer, 0, sizeof(buffer));

    res = msc_bot_transfer(msc, cdb, sizeof(cdb), buffer, sizeof(buffer), true);
    if (res == 0) {
        memcpy(msc->vendor, &buffer[8], 8);
        msc->vendor[8] = '\0';
        memcpy(msc->product, &buffer[16], 16);
        msc->product[16] = '\0';
        memcpy(msc->revision, &buffer[32], 4);
        msc->revision[4] = '\0';

        /* Trim trailing spaces */
        for (i = 7; i >= 0 && msc->vendor[i] == ' '; i--) msc->vendor[i] = '\0';
        for (i = 15; i >= 0 && msc->product[i] == ' '; i--) msc->product[i] = '\0';
        for (i = 3; i >= 0 && msc->revision[i] == ' '; i--) msc->revision[i] = '\0';
    }
    return res;
}

static int scsi_test_unit_ready(usb_msc_dev_t *msc) {
    uint8_t cdb[6];
    cdb[0] = SCSI_TEST_UNIT_READY;
    cdb[1] = 0;
    cdb[2] = 0;
    cdb[3] = 0;
    cdb[4] = 0;
    cdb[5] = 0;
    return msc_bot_transfer(msc, cdb, sizeof(cdb), NULL, 0, false);
}

static int scsi_read_capacity(usb_msc_dev_t *msc) {
    uint8_t cdb[10];
    uint32_t buffer[2];
    int res;

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_READ_CAPACITY10;
    memset(buffer, 0, sizeof(buffer));

    res = msc_bot_transfer(msc, cdb, sizeof(cdb), buffer, sizeof(buffer), true);
    if (res == 0) {
        uint32_t last_lba;
        uint32_t block_size;

        last_lba = be32(buffer[0]);
        block_size = be32(buffer[1]);

        msc->block_count = last_lba + 1;
        msc->block_size = block_size;
    }
    return res;
}

int usb_msc_read_blocks(usb_msc_dev_t *msc, uint32_t lba, uint32_t count, void *buf) {
    uint8_t *ptr;
    uint32_t remaining;
    uint32_t cur_lba;

    if (!msc || !msc->ready || count == 0 || !buf) return -1;

    ptr = (uint8_t*)buf;
    remaining = count;
    cur_lba = lba;

    while (remaining > 0) {
        uint32_t chunk;
        uint8_t cdb[10];
        uint32_t bytes;
        int res;
        int retries;

        chunk = (remaining > 64) ? 64 : remaining;
        bytes = chunk * msc->block_size;

        memset(cdb, 0, sizeof(cdb));
        cdb[0] = SCSI_READ10;
        cdb[2] = (uint8_t)((cur_lba >> 24) & 0xFF);
        cdb[3] = (uint8_t)((cur_lba >> 16) & 0xFF);
        cdb[4] = (uint8_t)((cur_lba >> 8) & 0xFF);
        cdb[5] = (uint8_t)(cur_lba & 0xFF);
        cdb[7] = (uint8_t)((chunk >> 8) & 0xFF);
        cdb[8] = (uint8_t)(chunk & 0xFF);

        res = -1;
        for (retries = 0; retries < 3; retries++) {
            res = msc_bot_transfer(msc, cdb, sizeof(cdb), ptr, bytes, true);
            if (res == 0) break;
            pit_delay_ms(10);
        }

        if (res != 0) return res;

        ptr += bytes;
        cur_lba += chunk;
        remaining -= chunk;
    }

    return 0;
}

int usb_msc_write_blocks(usb_msc_dev_t *msc, uint32_t lba, uint32_t count, const void *buf) {
    const uint8_t *ptr;
    uint32_t remaining;
    uint32_t cur_lba;

    if (!msc || !msc->ready || count == 0 || !buf) return -1;

    ptr = (const uint8_t*)buf;
    remaining = count;
    cur_lba = lba;

    while (remaining > 0) {
        uint32_t chunk;
        uint8_t cdb[10];
        uint32_t bytes;
        int res;
        int retries;

        chunk = (remaining > 64) ? 64 : remaining;
        bytes = chunk * msc->block_size;

        memset(cdb, 0, sizeof(cdb));
        cdb[0] = SCSI_WRITE10;
        cdb[2] = (uint8_t)((cur_lba >> 24) & 0xFF);
        cdb[3] = (uint8_t)((cur_lba >> 16) & 0xFF);
        cdb[4] = (uint8_t)((cur_lba >> 8) & 0xFF);
        cdb[5] = (uint8_t)(cur_lba & 0xFF);
        cdb[7] = (uint8_t)((chunk >> 8) & 0xFF);
        cdb[8] = (uint8_t)(chunk & 0xFF);

        res = -1;
        for (retries = 0; retries < 3; retries++) {
            res = msc_bot_transfer(msc, cdb, sizeof(cdb), (void*)ptr, bytes, false);
            if (res == 0) break;
            pit_delay_ms(10);
        }

        if (res != 0) return res;

        ptr += bytes;
        cur_lba += chunk;
        remaining -= chunk;
    }

    return 0;
}

static int bdev_read_wrapper(block_dev_t *bdev, uint32_t lba, uint32_t count, void *buf) {
    usb_msc_dev_t *msc;
    msc = (usb_msc_dev_t*)bdev->priv;
    return usb_msc_read_blocks(msc, lba, count, buf);
}

static int bdev_write_wrapper(block_dev_t *bdev, uint32_t lba, uint32_t count, const void *buf) {
    usb_msc_dev_t *msc;
    msc = (usb_msc_dev_t*)bdev->priv;
    return usb_msc_write_blocks(msc, lba, count, buf);
}

int usb_msc_init_device(usb_device_t *dev, usb_interface_t *iface) {
    usb_endpoint_t *in_ep;
    usb_endpoint_t *out_ep;
    uint8_t i;
    usb_msc_dev_t *msc;
    int retries;

    if (msc_count >= MAX_MSC_DEVS) return -1;

    in_ep = NULL;
    out_ep = NULL;

    for (i = 0; i < iface->num_endpoints; i++) {
        uint8_t type;
        type = iface->endpoints[i].attributes & 0x03;
        if (type == 0x02) { /* Bulk endpoint */
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

    msc = &msc_devs[msc_count];
    memset(msc, 0, sizeof(usb_msc_dev_t));

    msc->dev = dev;
    msc->in_dci = in_ep->dci;
    msc->out_dci = out_ep->dci;
    msc->ep_in_addr = in_ep->address;
    msc->ep_out_addr = out_ep->address;
    msc->max_packet_in = in_ep->max_packet_size;
    msc->max_packet_out = out_ep->max_packet_size;

    /* 1. Query Maximum Logical Unit Number (Max LUN) */
    msc_get_max_lun(msc);

    /* 2. Execute SCSI Inquiry with retries */
    for (retries = 0; retries < 3; retries++) {
        if (scsi_inquiry(msc) == 0) break;
        pit_delay_ms(50);
    }

    /* 3. Wait for drive ready (Test Unit Ready loop) */
    {
        int ready_retries;
        bool is_ready;

        is_ready = false;
        for (ready_retries = 0; ready_retries < 30; ready_retries++) {
            uint8_t sense_key;
            uint8_t asc;
            uint8_t ascq;

            sense_key = 0;
            asc = 0;
            ascq = 0;

            if (scsi_test_unit_ready(msc) == 0) {
                is_ready = true;
                break;
            }

            /* Read Sense Data to clear Unit Attention */
            scsi_request_sense(msc, &sense_key, &asc, &ascq);
            pit_delay_ms(100);
        }

        if (!is_ready) {
            kprintf("[MSC] Warning: Device on Slot %u not reporting READY, attempting READ CAPACITY...\n",
                    dev->slot_id);
        }
    }

    /* 4. Read Capacity with retries */
    {
        int cap_retries;
        bool cap_ok;

        cap_ok = false;
        for (cap_retries = 0; cap_retries < 5; cap_retries++) {
            if (scsi_read_capacity(msc) == 0 && msc->block_count > 0) {
                cap_ok = true;
                break;
            }
            pit_delay_ms(100);
        }

        if (!cap_ok) {
            kprint_color(0x4F, "[MSC] SCSI READ CAPACITY failed on Slot %u\n", dev->slot_id);
            return -1;
        }
    }

    if (msc->block_size == 0 || msc->block_size > 4096) {
        msc->block_size = 512;
    }

    msc->ready = true;
    snprintf(dev->name, sizeof(dev->name), "%s %s", msc->vendor, msc->product);

    kprintf("[MSC] USB Mass Storage initialized: '%s' '%s' (Rev %s)\n",
            msc->vendor, msc->product, msc->revision);
    kprintf("[MSC] Capacity: %u blocks of %u bytes (%u MB)\n",
            msc->block_count, msc->block_size,
            (msc->block_count / 1024) * msc->block_size / 1024);

    /* Register block device */
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

int usb_msc_remove_device(usb_device_t *dev) {
    size_t i;
    size_t j;

    if (!dev) return -1;

    for (i = 0; i < msc_count; i++) {
        if (msc_devs[i].dev == dev) {
            blockdev_unregister(&msc_devs[i].bdev);
            for (j = i; j < msc_count - 1; j++) {
                msc_devs[j] = msc_devs[j + 1];
            }
            msc_count--;
            return 0;
        }
    }
    return -1;
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
