#ifndef QEMU_PREP_PCI_H
#define QEMU_PREP_PCI_H

#include "qemu-common.h"
#include "memory.h"

PCIBus *pci_prep_init(qemu_irq *pic,
                      MemoryRegion *address_space_mem,
                      MemoryRegion *address_space_io);

#endif
