#ifndef QEMU_VMWARE_VGA_H
#define QEMU_VMWARE_VGA_H

#include "qemu-common.h"

/* vmware_vga.c */
static inline void pci_vmsvga_init(PCIBus *bus)
{
    pci_create_simple(bus, -1, "vmware-svga");
}

#endif
