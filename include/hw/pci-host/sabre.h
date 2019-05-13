#ifndef HW_PCI_HOST_SABRE_H
#define HW_PCI_HOST_SABRE_H

#include "hw/sparc/sun4u_iommu.h"

#define MAX_IVEC 0x40

/* OBIO IVEC IRQs */
#define OBIO_HDD_IRQ         0x20
#define OBIO_NIC_IRQ         0x21
#define OBIO_LPT_IRQ         0x22
#define OBIO_FDD_IRQ         0x27
#define OBIO_KBD_IRQ         0x29
#define OBIO_MSE_IRQ         0x2a
#define OBIO_SER_IRQ         0x2b

typedef struct SabrePCIState {
    PCIDevice parent_obj;
} SabrePCIState;

#define TYPE_SABRE_PCI_DEVICE "sabre-pci"
#define SABRE_PCI_DEVICE(obj) \
    OBJECT_CHECK(SabrePCIState, (obj), TYPE_SABRE_PCI_DEVICE)

typedef struct SabreState {
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
} SabreState;

#define TYPE_SABRE "sabre"
#define SABRE_DEVICE(obj) \
    OBJECT_CHECK(SabreState, (obj), TYPE_SABRE)

#endif
