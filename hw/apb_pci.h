#ifndef APB_PCI_H
#define APB_PCI_H

#include "qemu-common.h"

PCIBus *pci_apb_init(target_phys_addr_t special_base,
                     target_phys_addr_t mem_base,
                     qemu_irq *ivec_irqs, PCIBus **bus2, PCIBus **bus3,
                     qemu_irq **pbm_irqs);
#endif
