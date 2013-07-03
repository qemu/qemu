#ifndef QEMU_IOH3420_H
#define QEMU_IOH3420_H

#include "hw/pci/pcie_port.h"

PCIESlot *ioh3420_init(PCIBus *bus, int devfn, bool multifunction,
                       const char *bus_name, pci_map_irq_fn map_irq,
                       uint8_t port, uint8_t chassis, uint16_t slot);

#endif /* QEMU_IOH3420_H */
