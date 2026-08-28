/*
 * This is free and unencumbered software released into the public domain.
 * GEMIOS Preemptive Real-Time Operating System
 */

#include "pci.h"
#include "io.h"
#include "string.h"

static pci_device_t pci_devices[MAX_PCI_DEVICES];
static size_t pci_device_count = 0;

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address;
    address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address;
    address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    return (uint16_t)((inl(PCI_CONFIG_DATA) >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address;
    address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    return (uint8_t)((inl(PCI_CONFIG_DATA) >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address;
    address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, val);
}

void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val) {
    uint32_t address;
    uint32_t cur;
    uint32_t shift;

    address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    cur = inl(PCI_CONFIG_DATA);
    shift = (offset & 2) * 8;
    cur &= ~(0xFFFF << shift);
    cur |= ((uint32_t)val << shift);
    outl(PCI_CONFIG_DATA, cur);
}

void pci_write_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t val) {
    uint32_t address;
    uint32_t cur;
    uint32_t shift;

    address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    cur = inl(PCI_CONFIG_DATA);
    shift = (offset & 3) * 8;
    cur &= ~(0xFF << shift);
    cur |= ((uint32_t)val << shift);
    outl(PCI_CONFIG_DATA, cur);
}

void pci_enable_bus_mastering(pci_device_t *dev) {
    uint16_t cmd;
    cmd = pci_read_config16(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= (PCI_COMMAND_BUS_MASTER | PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_IO);
    cmd &= ~(1 << 10); /* Ensure INTx interrupts are enabled */
    pci_write_config16(dev->bus, dev->slot, dev->func, 0x04, cmd);
}

static void pci_probe_device(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t vendor_id;
    pci_device_t *dev;
    int max_bars;
    int bar_idx;

    vendor_id = pci_read_config16(bus, slot, func, 0x00);
    if (vendor_id == 0xFFFF || vendor_id == 0x0000) return;

    if (pci_device_count >= MAX_PCI_DEVICES) return;

    dev = &pci_devices[pci_device_count];
    memset(dev, 0, sizeof(pci_device_t));

    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = vendor_id;
    dev->device_id = pci_read_config16(bus, slot, func, 0x02);
    dev->class_code = pci_read_config8(bus, slot, func, 0x0B);
    dev->subclass = pci_read_config8(bus, slot, func, 0x0A);
    dev->prog_if = pci_read_config8(bus, slot, func, 0x09);
    dev->revision_id = pci_read_config8(bus, slot, func, 0x08);
    dev->header_type = pci_read_config8(bus, slot, func, 0x0E);
    dev->irq_pin = pci_read_config8(bus, slot, func, 0x3D);
    dev->irq_line = pci_read_config8(bus, slot, func, 0x3C);

    /* Read BARs */
    max_bars = (dev->header_type & 0x7F) == 0 ? 6 : 2;
    for (bar_idx = 0; bar_idx < max_bars; bar_idx++) {
        uint8_t bar_offset;
        uint32_t bar_val;

        bar_offset = 0x10 + (bar_idx * 4);
        bar_val = pci_read_config32(bus, slot, func, bar_offset);

        if (bar_val == 0) continue;

        if (bar_val & 0x01) {
            /* I/O Space BAR */
            dev->bar_type[bar_idx] = 1;
            dev->bar[bar_idx] = bar_val & ~0x03;
        } else {
            uint8_t mem_type;
            uint32_t size_mask;
            uint16_t old_cmd;

            /* Memory Space BAR */
            dev->bar_type[bar_idx] = 0;
            mem_type = (bar_val >> 1) & 0x03;
            dev->bar[bar_idx] = bar_val & ~0x0F;

            /* Safely probe BAR size by temporarily masking decoding bits */
            old_cmd = pci_read_config16(bus, slot, func, 0x04);
            pci_write_config16(bus, slot, func, 0x04, old_cmd & ~(PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_IO));
            pci_write_config32(bus, slot, func, bar_offset, 0xFFFFFFFF);
            size_mask = pci_read_config32(bus, slot, func, bar_offset);
            pci_write_config32(bus, slot, func, bar_offset, bar_val); /* Restore BAR */
            pci_write_config16(bus, slot, func, 0x04, old_cmd); /* Restore Command */

            if (size_mask != 0 && size_mask != 0xFFFFFFFF) {
                dev->bar_size[bar_idx] = ~(size_mask & ~0x0F) + 1;
            }

            if (mem_type == 0x02) {
                /* 64-bit BAR */
                uint32_t bar_high = 0;
                if (bar_idx + 1 < max_bars) {
                    bar_high = pci_read_config32(bus, slot, func, (uint8_t)(bar_offset + 4));
                }

                if (bar_high != 0) {
                    /* BAR is mapped above 4GB! Remap it below 4GB */
                    static uint32_t s_gemios_next_mmio = 0xD8000000;
                    uint32_t bar_len = dev->bar_size[bar_idx];
                    uint32_t new_base;

                    if (bar_len == 0 || bar_len > 0x10000000) {
                        bar_len = 0x00010000; /* 64 KB fallback */
                    }

                    s_gemios_next_mmio = (s_gemios_next_mmio - bar_len) & ~(bar_len - 1);
                    new_base = s_gemios_next_mmio;

                    /* Disable memory decoding */
                    pci_write_config16(bus, slot, func, 0x04, old_cmd & ~PCI_COMMAND_MEMORY_SPACE);

                    /* Reprogram BAR low & high */
                    pci_write_config32(bus, slot, func, bar_offset, new_base | (bar_val & 0x0F));
                    if (bar_idx + 1 < max_bars) {
                        pci_write_config32(bus, slot, func, (uint8_t)(bar_offset + 4), 0);
                    }

                    /* Restore / enable memory decoding */
                    pci_write_config16(bus, slot, func, 0x04, old_cmd | PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER);

                    dev->bar[bar_idx] = new_base;
                    kprintf("[PCI] Remapped >4GB 64-bit BAR on %02x:%02x.%u to 0x%08x (len=%u KB)\n",
                            bus, slot, func, new_base, bar_len / 1024);
                }

                /* skip next BAR slot for 64-bit BAR */
                bar_idx++;
            }
        }
    }

    pci_device_count++;
}

void pci_init(void) {
    uint16_t bus;
    uint8_t slot;

    pci_device_count = 0;

    for (bus = 0; bus < 256; bus++) {
        for (slot = 0; slot < 32; slot++) {
            uint16_t vendor;
            uint8_t header_type;

            vendor = pci_read_config16((uint8_t)bus, slot, 0, 0x00);
            if (vendor == 0xFFFF) continue;

            pci_probe_device((uint8_t)bus, slot, 0);

            header_type = pci_read_config8((uint8_t)bus, slot, 0, 0x0E);
            if (header_type & 0x80) {
                /* Multi-function device */
                uint8_t func;
                for (func = 1; func < 8; func++) {
                    if (pci_read_config16((uint8_t)bus, slot, func, 0x00) != 0xFFFF) {
                        pci_probe_device((uint8_t)bus, slot, func);
                    }
                }
            }
        }
    }
}

pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    size_t i;
    for (i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id && pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if) {
    size_t i;
    for (i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].class_code == class_code &&
            pci_devices[i].subclass == subclass &&
            (prog_if == 0xFF || pci_devices[i].prog_if == prog_if)) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

size_t pci_get_device_count(void) {
    return pci_device_count;
}

pci_device_t *pci_get_device(size_t index) {
    if (index < pci_device_count) {
        return &pci_devices[index];
    }
    return NULL;
}

const char *pci_class_to_string(uint8_t class_code, uint8_t subclass, uint8_t prog_if) {
    if (class_code == 0x01) {
        if (subclass == 0x01) return "IDE Controller";
        if (subclass == 0x06) return "SATA AHCI Controller";
        return "Mass Storage";
    }
    if (class_code == 0x02) return "Network Controller";
    if (class_code == 0x03) return "VGA Display Adapter";
    if (class_code == 0x06) return "PCI Bridge";
    if (class_code == 0x0C && subclass == 0x03) {
        if (prog_if == 0x00) return "USB UHCI Controller";
        if (prog_if == 0x10) return "USB OHCI Controller";
        if (prog_if == 0x20) return "USB EHCI Controller";
        if (prog_if == 0x30) return "USB xHCI Controller";
        return "USB Controller";
    }
    return "Unknown Device";
}
