#ifndef PCI_HOST_APB_H
#define PCI_HOST_APB_H

#include "qemu-common.h"
#include "hw/pci/pci_host.h"

#define IOMMU_NREGS             3

#define IOMMU_PAGE_SIZE_8K      (1ULL << 13)
#define IOMMU_PAGE_MASK_8K      (~(IOMMU_PAGE_SIZE_8K - 1))
#define IOMMU_PAGE_SIZE_64K     (1ULL << 16)
#define IOMMU_PAGE_MASK_64K     (~(IOMMU_PAGE_SIZE_64K - 1))

#define IOMMU_CTRL              0x0
#define IOMMU_CTRL_TBW_SIZE     (1ULL << 2)
#define IOMMU_CTRL_MMU_EN       (1ULL)

#define IOMMU_CTRL_TSB_SHIFT    16

#define IOMMU_BASE              0x8
#define IOMMU_FLUSH             0x10

#define IOMMU_TTE_DATA_V        (1ULL << 63)
#define IOMMU_TTE_DATA_SIZE     (1ULL << 61)
#define IOMMU_TTE_DATA_W        (1ULL << 1)

#define IOMMU_TTE_PHYS_MASK_8K  0x1ffffffe000ULL
#define IOMMU_TTE_PHYS_MASK_64K 0x1ffffff8000ULL

#define IOMMU_TSB_8K_OFFSET_MASK_8M    0x00000000007fe000ULL
#define IOMMU_TSB_8K_OFFSET_MASK_16M   0x0000000000ffe000ULL
#define IOMMU_TSB_8K_OFFSET_MASK_32M   0x0000000001ffe000ULL
#define IOMMU_TSB_8K_OFFSET_MASK_64M   0x0000000003ffe000ULL
#define IOMMU_TSB_8K_OFFSET_MASK_128M  0x0000000007ffe000ULL
#define IOMMU_TSB_8K_OFFSET_MASK_256M  0x000000000fffe000ULL
#define IOMMU_TSB_8K_OFFSET_MASK_512M  0x000000001fffe000ULL
#define IOMMU_TSB_8K_OFFSET_MASK_1G    0x000000003fffe000ULL

#define IOMMU_TSB_64K_OFFSET_MASK_64M  0x0000000003ff0000ULL
#define IOMMU_TSB_64K_OFFSET_MASK_128M 0x0000000007ff0000ULL
#define IOMMU_TSB_64K_OFFSET_MASK_256M 0x000000000fff0000ULL
#define IOMMU_TSB_64K_OFFSET_MASK_512M 0x000000001fff0000ULL
#define IOMMU_TSB_64K_OFFSET_MASK_1G   0x000000003fff0000ULL
#define IOMMU_TSB_64K_OFFSET_MASK_2G   0x000000007fff0000ULL

typedef struct IOMMUState {
    SysBusDevice parent_obj;

    AddressSpace iommu_as;
    IOMMUMemoryRegion iommu;

    MemoryRegion iomem;
    uint64_t regs[IOMMU_NREGS];
} IOMMUState;

#define TYPE_SUN4U_IOMMU "sun4u-iommu"
#define SUN4U_IOMMU(obj) OBJECT_CHECK(IOMMUState, (obj), TYPE_SUN4U_IOMMU)

#define MAX_IVEC 0x40

/* OBIO IVEC IRQs */
#define OBIO_HDD_IRQ         0x20
#define OBIO_NIC_IRQ         0x21
#define OBIO_LPT_IRQ         0x22
#define OBIO_FDD_IRQ         0x27
#define OBIO_KBD_IRQ         0x29
#define OBIO_MSE_IRQ         0x2a
#define OBIO_SER_IRQ         0x2b

#define TYPE_APB "pbm"

#define APB_DEVICE(obj) \
    OBJECT_CHECK(APBState, (obj), TYPE_APB)

#define TYPE_APB_IOMMU_MEMORY_REGION "pbm-iommu-memory-region"

typedef struct APBState {
    PCIHostState parent_obj;

    hwaddr special_base;
    hwaddr mem_base;
    MemoryRegion apb_config;
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
} APBState;

typedef struct PBMPCIBridge {
    /*< private >*/
    PCIBridge parent_obj;
} PBMPCIBridge;

#define TYPE_PBM_PCI_BRIDGE "pbm-bridge"
#define PBM_PCI_BRIDGE(obj) \
    OBJECT_CHECK(PBMPCIBridge, (obj), TYPE_PBM_PCI_BRIDGE)

#endif
