#ifndef QEMU_PCI_BUS_H
#define QEMU_PCI_BUS_H

/*
 * PCI Bus and Bridge datastructures.
 *
 * Do not access the following members directly;
 * use accessor functions in pci.h, pci_bridge.h
 */

struct PCIBus {
    BusState qbus;
    PCIIOMMUFunc iommu_fn;
    void *iommu_opaque;
    uint8_t devfn_min;
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
};

typedef struct PCIBridgeWindows PCIBridgeWindows;

/*
 * Aliases for each of the address space windows that the bridge
 * can forward. Mapped into the bridge's parent's address space,
 * as subregions.
 */
struct PCIBridgeWindows {
    MemoryRegion alias_pref_mem;
    MemoryRegion alias_mem;
    MemoryRegion alias_io;
    /*
     * When bridge control VGA forwarding is enabled, bridges will
     * provide positive decode on the PCI VGA defined I/O port and
     * MMIO ranges.  When enabled forwarding is only qualified on the
     * I/O and memory enable bits in the bridge command register.
     */
    MemoryRegion alias_vga[QEMU_PCI_VGA_NUM_REGIONS];
};

#define TYPE_PCI_BRIDGE "base-pci-bridge"
#define PCI_BRIDGE(obj) OBJECT_CHECK(PCIBridge, (obj), TYPE_PCI_BRIDGE)

struct PCIBridge {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    /* private member */
    PCIBus sec_bus;
    /*
     * Memory regions for the bridge's address spaces.  These regions are not
     * directly added to system_memory/system_io or its descendants.
     * Bridge's secondary bus points to these, so that devices
     * under the bridge see these regions as its address spaces.
     * The regions are as large as the entire address space -
     * they don't take into account any windows.
     */
    MemoryRegion address_space_mem;
    MemoryRegion address_space_io;

    PCIBridgeWindows *windows;

    pci_map_irq_fn map_irq;
    const char *bus_name;
};

#endif /* QEMU_PCI_BUS_H */
