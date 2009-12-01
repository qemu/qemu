#ifndef QEMU_SH_PCI_H
#define QEMU_SH_PCI_H

#include "qemu-common.h"

PCIBus *sh_pci_register_bus(pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                            void *pic, int devfn_min, int nirq);

#endif
