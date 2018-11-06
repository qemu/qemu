#ifndef QEMU_PCI_BUS_H
#define QEMU_PCI_BUS_H

#include "hw/pci/pci.h"

/*
 * PCI Bus datastructures.
 *
 * Do not access the following members directly;
 * use accessor functions in pci.h
 */

typedef struct PCIBusClass {
    /*< private >*/
    BusClass parent_class;
    /*< public >*/

    bool (*is_root)(PCIBus *bus);
    int (*bus_num)(PCIBus *bus);
    uint16_t (*numa_node)(PCIBus *bus);
} PCIBusClass;

struct PCIBus {
    BusState qbus;
    PCIIOMMUFunc iommu_fn;
    void *iommu_opaque;
    uint8_t devfn_min;
    uint32_t slot_reserved_mask;
    pci_set_irq_fn set_irq;
    pci_map_irq_fn map_irq;
    pci_route_irq_fn route_intx_to_irq;
    void *irq_opaque;
    PCIDevice *devices[PCI_SLOT_MAX * PCI_FUNC_MAX];
    PCIDevice *parent_dev;
    MemoryRegion *address_space_mem;
    MemoryRegion *address_space_io;

    QLIST_HEAD(, PCIBus) child; /* this will be replaced by qdev later */
    QLIST_ENTRY(PCIBus) sibling;/* this will be replaced by qdev later */

    /* The bus IRQ state is the logical OR of the connected devices.
       Keep a count of the number of devices with raised IRQs.  */
    int nirq;
    int *irq_count;

    Notifier machine_done;
};

#endif /* QEMU_PCI_BUS_H */
