/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_USB_DEFS_H
#define GEMIOS_USB_DEFS_H

#include "types.h"

/* USB Device Speeds */
#define USB_SPEED_UNKNOWN       0
#define USB_SPEED_FULL          1 /* 12 Mbps */
#define USB_SPEED_LOW           2 /* 1.5 Mbps */
#define USB_SPEED_HIGH          3 /* 480 Mbps */
#define USB_SPEED_SUPER         4 /* 5 Gbps */
#define USB_SPEED_SUPER_PLUS    5 /* 10 Gbps */

/* USB Standard Request Types */
#define USB_REQ_TYPE_STANDARD   (0x00 << 5)
#define USB_REQ_TYPE_CLASS      (0x01 << 5)
#define USB_REQ_TYPE_VENDOR     (0x02 << 5)

#define USB_REQ_RECIPIENT_DEVICE    0x00
#define USB_REQ_RECIPIENT_INTERFACE 0x01
#define USB_REQ_RECIPIENT_ENDPOINT  0x02
#define USB_REQ_RECIPIENT_OTHER     0x03

#define USB_DIR_OUT             0x00
#define USB_DIR_IN              0x80

/* USB Standard Request Codes */
#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE     0x0A
#define USB_REQ_SET_INTERFACE     0x0B
#define USB_REQ_SYNCH_FRAME       0x0C

/* USB Descriptor Types */
#define USB_DESC_DEVICE                    0x01
#define USB_DESC_CONFIGURATION             0x02
#define USB_DESC_STRING                    0x03
#define USB_DESC_INTERFACE                 0x04
#define USB_DESC_ENDPOINT                  0x05
#define USB_DESC_DEVICE_QUALIFIER          0x06
#define USB_DESC_OTHER_SPEED_CONFIGURATION 0x07
#define USB_DESC_INTERFACE_POWER           0x08
#define USB_DESC_OTG                       0x09
#define USB_DESC_DEBUG                     0x0A
#define USB_DESC_INTERFACE_ASSOCIATION     0x0B
#define USB_DESC_BOS                       0x0F
#define USB_DESC_DEVICE_CAPABILITY         0x10
#define USB_DESC_HID                       0x21
#define USB_DESC_HID_REPORT                0x22
#define USB_DESC_HID_PHYSICAL              0x23
#define USB_DESC_HUB                       0x29
#define USB_DESC_SS_HUB                    0x2A
#define USB_DESC_SS_EP_COMPANION           0x30

/* USB Device Classes */
#define USB_CLASS_PER_INTERFACE   0x00
#define USB_CLASS_AUDIO           0x01
#define USB_CLASS_COMM            0x02
#define USB_CLASS_HID             0x03
#define USB_CLASS_PHYSICAL        0x05
#define USB_CLASS_IMAGE           0x06
#define USB_CLASS_PRINTER         0x07
#define USB_CLASS_MASS_STORAGE    0x08
#define USB_CLASS_HUB             0x09
#define USB_CLASS_DATA            0x0A
#define USB_CLASS_SMART_CARD      0x0B
#define USB_CLASS_VIDEO           0x0E
#define USB_CLASS_HEALTHCARE      0x0F
#define USB_CLASS_WIRELESS        0xE0
#define USB_CLASS_MISC            0xEF
#define USB_CLASS_VENDOR_SPEC     0xFF

/* HID Subclasses & Protocols */
#define USB_HID_SUBCLASS_BOOT     0x01
#define USB_HID_PROTOCOL_NONE     0x00
#define USB_HID_PROTOCOL_KEYBOARD 0x01
#define USB_HID_PROTOCOL_MOUSE    0x02

/* HID Class Requests */
#define USB_HID_REQ_GET_REPORT    0x01
#define USB_HID_REQ_GET_IDLE      0x02
#define USB_HID_REQ_GET_PROTOCOL  0x03
#define USB_HID_REQ_SET_REPORT    0x09
#define USB_HID_REQ_SET_IDLE      0x0A
#define USB_HID_REQ_SET_PROTOCOL  0x0B

/* Mass Storage Subclasses & Protocols */
#define USB_MSC_SUBCLASS_RBC      0x01
#define USB_MSC_SUBCLASS_ATAPI    0x02
#define USB_MSC_SUBCLASS_QIC_157  0x03
#define USB_MSC_SUBCLASS_UFI      0x04
#define USB_MSC_SUBCLASS_SFF_8070i 0x05
#define USB_MSC_SUBCLASS_SCSI     0x06

#define USB_MSC_PROTO_CBI_INT     0x00
#define USB_MSC_PROTO_CBI_NO_INT  0x01
#define USB_MSC_PROTO_BOT         0x50 /* Bulk-Only Transport */

/* Hub Class Requests & Features */
#define USB_HUB_REQ_GET_STATUS     0x00
#define USB_HUB_REQ_CLEAR_FEATURE  0x01
#define USB_HUB_REQ_SET_FEATURE    0x03
#define USB_HUB_REQ_GET_DESCRIPTOR 0x06
#define USB_HUB_REQ_SET_DESCRIPTOR 0x07
#define USB_HUB_REQ_CLEAR_TT_BUFFER 0x08
#define USB_HUB_REQ_RESET_TT       0x09
#define USB_HUB_REQ_GET_TT_STATE   0x0A
#define USB_HUB_REQ_STOP_TT        0x0B

#define USB_HUB_FEAT_PORT_CONNECTION  0
#define USB_HUB_FEAT_PORT_ENABLE      1
#define USB_HUB_FEAT_PORT_SUSPEND     2
#define USB_HUB_FEAT_PORT_OVER_CURRENT 3
#define USB_HUB_FEAT_PORT_RESET       4
#define USB_HUB_FEAT_PORT_POWER       8
#define USB_HUB_FEAT_PORT_LOW_SPEED   9
#define USB_HUB_FEAT_C_PORT_CONNECTION 16
#define USB_HUB_FEAT_C_PORT_ENABLE    17
#define USB_HUB_FEAT_C_PORT_SUSPEND   18
#define USB_HUB_FEAT_C_PORT_OVER_CURRENT 19
#define USB_HUB_FEAT_C_PORT_RESET     20

#define USB_HUB_PORT_STAT_CONNECTION  (1 << 0)
#define USB_HUB_PORT_STAT_ENABLE      (1 << 1)
#define USB_HUB_PORT_STAT_SUSPEND     (1 << 2)
#define USB_HUB_PORT_STAT_OVERCURRENT (1 << 3)
#define USB_HUB_PORT_STAT_RESET       (1 << 4)
#define USB_HUB_PORT_STAT_POWER       (1 << 8)
#define USB_HUB_PORT_STAT_LOW_SPEED   (1 << 9)
#define USB_HUB_PORT_STAT_HIGH_SPEED  (1 << 10)

/* USB Setup Packet Structure (8 bytes) */
struct usb_setup_packet {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} PACKED;
typedef struct usb_setup_packet usb_setup_packet_t;

/* Standard Device Descriptor (18 bytes) */
struct usb_device_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} PACKED;
typedef struct usb_device_descriptor usb_device_descriptor_t;

/* Standard Configuration Descriptor (9 bytes) */
struct usb_config_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} PACKED;
typedef struct usb_config_descriptor usb_config_descriptor_t;

/* Standard Interface Descriptor (9 bytes) */
struct usb_interface_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} PACKED;
typedef struct usb_interface_descriptor usb_interface_descriptor_t;

/* Standard Endpoint Descriptor (7 bytes) */
struct usb_endpoint_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress; /* Bit 7: 1=IN, 0=OUT; Bits 0..3: EP number */
    uint8_t  bmAttributes;     /* Bits 0..1: 00=Control, 01=Isoch, 10=Bulk, 11=Interrupt */
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} PACKED;
typedef struct usb_endpoint_descriptor usb_endpoint_descriptor_t;

/* HID Descriptor */
struct usb_hid_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdHID;
    uint8_t  bCountryCode;
    uint8_t  bNumDescriptors;
    uint8_t  bReportDescriptorType;
    uint16_t wReportDescriptorLength;
} PACKED;
typedef struct usb_hid_descriptor usb_hid_descriptor_t;

/* USB 2.0 Hub Descriptor */
struct usb_hub_descriptor {
    uint8_t  bDescLength;
    uint8_t  bDescriptorType;
    uint8_t  bNbrPorts;
    uint16_t wHubCharacteristics;
    uint8_t  bPwrOn2PwrGood;
    uint8_t  bHubContrCurrent;
    uint8_t  device_removable[8];
} PACKED;
typedef struct usb_hub_descriptor usb_hub_descriptor_t;

const char *usb_speed_to_string(uint8_t speed);
const char *usb_class_to_string(uint8_t class_code);

#endif /* GEMIOS_USB_DEFS_H */
