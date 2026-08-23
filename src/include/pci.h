/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#ifndef GEMIOS_PCI_H
#define GEMIOS_PCI_H

#include "types.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_COMMAND_IO          (1 << 0)
#define PCI_COMMAND_MEMORY_SPACE (1 << 1)
#define PCI_COMMAND_BUS_MASTER  (1 << 2)

#define PCI_CLASS_MASS_STORAGE  0x01
#define PCI_CLASS_NETWORK       0x02
#define PCI_CLASS_DISPLAY       0x03
#define PCI_CLASS_BRIDGE        0x06
#define PCI_CLASS_SERIAL_BUS    0x0C

#define PCI_SUBCLASS_USB        0x03

#define PCI_PROGIF_UHCI         0x00
#define PCI_PROGIF_OHCI         0x10
#define PCI_PROGIF_EHCI         0x20
#define PCI_PROGIF_XHCI         0x30

struct pci_device {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision_id;
    uint8_t  header_type;
    uint32_t bar[6];
    uint32_t bar_size[6];
    uint8_t  bar_type[6]; /* 0=Memory, 1=IO */
    uint8_t  irq_pin;
    uint8_t  irq_line;
};

typedef struct pci_device pci_device_t;

#define MAX_PCI_DEVICES 64

void pci_init(void);
uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t val);
void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);
void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);

void pci_enable_bus_mastering(pci_device_t *dev);
pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id);
pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if);

size_t pci_get_device_count(void);
pci_device_t *pci_get_device(size_t index);
const char *pci_class_to_string(uint8_t class_code, uint8_t subclass, uint8_t prog_if);

#endif /* GEMIOS_PCI_H */
