#ifndef QEMU_PREP_PCI_H
#define QEMU_PREP_PCI_H

#include "qemu-common.h"

PCIBus *pci_prep_init(qemu_irq *pic);

#endif
