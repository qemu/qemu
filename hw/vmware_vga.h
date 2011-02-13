#ifndef QEMU_VMWARE_VGA_H
#define QEMU_VMWARE_VGA_H

#include "qemu-common.h"

/* vmware_vga.c */
static inline bool pci_vmsvga_init(PCIBus *bus)
{
    PCIDevice *dev;

    dev = pci_try_create(bus, -1, "vmware-svga");
    if (!dev || qdev_init(&dev->qdev) < 0) {
        return false;
    } else {
        return true;
    }
}

#endif
