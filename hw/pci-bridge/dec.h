#ifndef HW_PCI_BRIDGE_DEC_H
#define HW_PCI_BRIDGE_DEC_H


#define TYPE_DEC_21154 "dec-21154-sysbus"

PCIBus *pci_dec_21154_init(PCIBus *parent_bus, int devfn);

#endif
