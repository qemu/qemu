#ifndef QEMU_XIO3130_UPSTREAM_H
#define QEMU_XIO3130_UPSTREAM_H

#include "pcie_port.h"

PCIEPort *xio3130_upstream_init(PCIBus *bus, int devfn, bool multifunction,
                                const char *bus_name, pci_map_irq_fn map_irq,
                                uint8_t port);

#endif /* QEMU_XIO3130_H */
