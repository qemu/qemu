#ifndef QEMU_XIO3130_DOWNSTREAM_H
#define QEMU_XIO3130_DOWNSTREAM_H

#include "pcie_port.h"

PCIESlot *xio3130_downstream_init(PCIBus *bus, int devfn, bool multifunction,
                                  const char *bus_name, pci_map_irq_fn map_irq,
                                  uint8_t port, uint8_t chassis,
                                  uint16_t slot);

#endif /* QEMU_XIO3130_DOWNSTREAM_H */
