#ifndef DEC_PCI_H
#define DEC_PCI_H

#include "qemu-common.h"

#define TYPE_DEC_21154 "dec-21154-sysbus"

PCIBus *pci_dec_21154_init(PCIBus *parent_bus, int devfn);

#endif
