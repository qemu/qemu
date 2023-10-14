/*
 * HP-PARISC Astro Bus connector with Elroy PCI host bridges
 */

#ifndef ASTRO_H
#define ASTRO_H

#include "hw/pci/pci_host.h"

#define ASTRO_HPA               0xfed00000

#define ROPES_PER_IOC           8       /* per Ike half or Pluto/Astro */

#define TYPE_ASTRO_CHIP "astro-chip"
OBJECT_DECLARE_SIMPLE_TYPE(AstroState, ASTRO_CHIP)

#define TYPE_ELROY_PCI_HOST_BRIDGE "elroy-pcihost"
OBJECT_DECLARE_SIMPLE_TYPE(ElroyState, ELROY_PCI_HOST_BRIDGE)

#define ELROY_NUM               4 /* # of Elroys */
#define ELROY_IRQS              8 /* IOSAPIC IRQs */

/* ASTRO Memory and I/O regions */
#define LMMIO_DIST_BASE_ADDR      0xf4000000ULL
#define LMMIO_DIST_BASE_SIZE       0x4000000ULL

#define IOS_DIST_BASE_ADDR      0xfffee00000ULL
#define IOS_DIST_BASE_SIZE           0x10000ULL

struct AstroState;

struct ElroyState {
    PCIHostState parent_obj;

    /* parent Astro device */
    struct AstroState *astro;

    /* HPA of this Elroy */
    hwaddr hpa;

    /* PCI bus number (Elroy number) */
    unsigned int pci_bus_num;

    uint64_t config_address;
    uint64_t config_reg_elroy;

    uint64_t status_control;
    uint64_t arb_mask;
    uint64_t mmio_base[(0x0250 - 0x200) / 8];
    uint64_t error_config;

    uint32_t iosapic_reg_select;
    uint64_t iosapic_reg[0x20];

    uint32_t ilr;

    MemoryRegion this_mem;

    MemoryRegion pci_mmio;
    MemoryRegion pci_mmio_alias;
    MemoryRegion pci_hole;
    MemoryRegion pci_io;
};

struct AstroState {
    PCIHostState parent_obj;

    uint64_t ioc_ctrl;
    uint64_t ioc_status_ctrl;
    uint64_t ioc_ranges[(0x03d8 - 0x300) / 8];
    uint64_t ioc_rope_config;
    uint64_t ioc_status_control;
    uint64_t ioc_flush_control;
    uint64_t ioc_rope_control[8];
    uint64_t tlb_ibase;
    uint64_t tlb_imask;
    uint64_t tlb_pcom;
    uint64_t tlb_tcnfg;
    uint64_t tlb_pdir_base;

    struct ElroyState *elroy[ELROY_NUM];

    MemoryRegion this_mem;

    MemoryRegion pci_mmio;
    MemoryRegion pci_io;

    IOMMUMemoryRegion iommu;
    AddressSpace iommu_as;
};

#endif
