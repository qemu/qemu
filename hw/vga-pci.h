#ifndef VGA_PCI_H
#define VGA_PCI_H

#include "qemu-common.h"

/* vga-pci.c */
DeviceState *pci_vga_init(PCIBus *bus);

/* cirrus_vga.c */
DeviceState *pci_cirrus_vga_init(PCIBus *bus);

#endif
