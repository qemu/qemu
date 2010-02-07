#ifndef DEC_PCI_H
#define DEC_PCI_H

#include "qemu-common.h"

PCIBus *pci_dec_21154_init(PCIBus *parent_bus, int devfn);

#endif
