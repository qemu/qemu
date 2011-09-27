#ifndef QEMU_VMWARE_VGA_H
#define QEMU_VMWARE_VGA_H

#include "qemu-common.h"

/* vmware_vga.c */
static inline DeviceState *pci_vmsvga_init(PCIBus *bus)
{
    PCIDevice *dev;

    dev = pci_create_simple(bus, -1, "vmware-svga");
    return &dev->qdev;
}

#endif
