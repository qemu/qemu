#ifndef HW_PCI_HOST_SABRE_H
#define HW_PCI_HOST_SABRE_H

#include "hw/pci/pci_device.h"
#include "hw/pci/pci_host.h"
#include "hw/sparc/sun4u_iommu.h"
#include "qom/object.h"

#define MAX_IVEC 0x40

/* OBIO IVEC IRQs */
#define OBIO_HDD_IRQ         0x20
#define OBIO_NIC_IRQ         0x21
#define OBIO_LPT_IRQ         0x22
#define OBIO_FDD_IRQ         0x27
#define OBIO_KBD_IRQ         0x29
#define OBIO_MSE_IRQ         0x2a
#define OBIO_SER_IRQ         0x2b

struct SabrePCIState {
    PCIDevice parent_obj;
};

#define TYPE_SABRE_PCI_DEVICE "sabre-pci"
OBJECT_DECLARE_SIMPLE_TYPE(SabrePCIState, SABRE_PCI_DEVICE)

struct SabreState {
    PCIHostState parent_obj;

    hwaddr special_base;
    hwaddr mem_base;
    MemoryRegion sabre_config;
    MemoryRegion pci_config;
    MemoryRegion pci_mmio;
    MemoryRegion pci_ioport;
    uint64_t pci_irq_in;
    IOMMUState *iommu;
    PCIBridge *bridgeA;
    PCIBridge *bridgeB;
    uint32_t pci_control[16];
    uint32_t pci_irq_map[8];
    uint32_t pci_err_irq_map[4];
    uint32_t obio_irq_map[32];
    qemu_irq ivec_irqs[MAX_IVEC];
    unsigned int irq_request;
    uint32_t reset_control;
    unsigned int nr_resets;
};

#define TYPE_SABRE "sabre"
OBJECT_DECLARE_SIMPLE_TYPE(SabreState, SABRE)

#endif
