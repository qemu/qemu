/*
 * QEMU PowerPC PowerNV (POWER8) PHB3 model
 *
 * Copyright (c) 2014-2020, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PCI_HOST_PNV_PHB3_H
#define PCI_HOST_PNV_PHB3_H

#include "hw/pci/pcie_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/ppc/xics.h"
#include "qom/object.h"
#include "hw/pci-host/pnv_phb.h"

typedef struct PnvPHB3 PnvPHB3;
typedef struct PnvChip PnvChip;

/*
 * PHB3 XICS Source for MSIs
 */
#define TYPE_PHB3_MSI "phb3-msi"
typedef struct Phb3MsiState Phb3MsiState;
DECLARE_INSTANCE_CHECKER(Phb3MsiState, PHB3_MSI,
                         TYPE_PHB3_MSI)

#define PHB3_MAX_MSI     2048

struct Phb3MsiState {
    ICSState ics;
    qemu_irq *qirqs;

    PnvPHB3 *phb;
    uint64_t rba[PHB3_MAX_MSI / 64];
    uint32_t rba_sum;
};

void pnv_phb3_msi_update_config(Phb3MsiState *msis, uint32_t base,
                                uint32_t count);
void pnv_phb3_msi_send(Phb3MsiState *msis, uint64_t addr, uint16_t data,
                       int32_t dev_pe);
void pnv_phb3_msi_ffi(Phb3MsiState *msis, uint64_t val);
void pnv_phb3_msi_pic_print_info(Phb3MsiState *msis, Monitor *mon);


/*
 * We have one such address space wrapper per possible device under
 * the PHB since they need to be assigned statically at qemu device
 * creation time. The relationship to a PE is done later dynamically.
 * This means we can potentially create a lot of these guys. Q35
 * stores them as some kind of radix tree but we never really need to
 * do fast lookups so instead we simply keep a QLIST of them for now,
 * we can add the radix if needed later on.
 *
 * We do cache the PE number to speed things up a bit though.
 */
typedef struct PnvPhb3DMASpace {
    PCIBus *bus;
    uint8_t devfn;
    int pe_num;         /* Cached PE number */
#define PHB_INVALID_PE (-1)
    PnvPHB3 *phb;
    AddressSpace dma_as;
    IOMMUMemoryRegion dma_mr;
    MemoryRegion msi32_mr;
    MemoryRegion msi64_mr;
    QLIST_ENTRY(PnvPhb3DMASpace) list;
} PnvPhb3DMASpace;

/*
 * PHB3 Power Bus Common Queue
 */
#define TYPE_PNV_PBCQ "pnv-pbcq"
OBJECT_DECLARE_SIMPLE_TYPE(PnvPBCQState, PNV_PBCQ)

struct PnvPBCQState {
    DeviceState parent;

    uint32_t nest_xbase;
    uint32_t spci_xbase;
    uint32_t pci_xbase;
#define PBCQ_NEST_REGS_COUNT    0x46
#define PBCQ_PCI_REGS_COUNT     0x15
#define PBCQ_SPCI_REGS_COUNT    0x5

    uint64_t nest_regs[PBCQ_NEST_REGS_COUNT];
    uint64_t spci_regs[PBCQ_SPCI_REGS_COUNT];
    uint64_t pci_regs[PBCQ_PCI_REGS_COUNT];
    MemoryRegion mmbar0;
    MemoryRegion mmbar1;
    MemoryRegion phbbar;
    uint64_t mmio0_base;
    uint64_t mmio0_size;
    uint64_t mmio1_base;
    uint64_t mmio1_size;
    PnvPHB3 *phb;

    MemoryRegion xscom_nest_regs;
    MemoryRegion xscom_pci_regs;
    MemoryRegion xscom_spci_regs;
};

/*
 * PHB3 PCIe Root Bus
 */
#define TYPE_PNV_PHB3_ROOT_BUS "pnv-phb3-root"
struct PnvPHB3RootBus {
    PCIBus parent;

    uint32_t chip_id;
    uint32_t phb_id;
};
OBJECT_DECLARE_SIMPLE_TYPE(PnvPHB3RootBus, PNV_PHB3_ROOT_BUS)

/*
 * PHB3 PCIe Host Bridge for PowerNV machines (POWER8)
 */
#define TYPE_PNV_PHB3 "pnv-phb3"
OBJECT_DECLARE_SIMPLE_TYPE(PnvPHB3, PNV_PHB3)

#define PNV_PHB3_NUM_M64      16
#define PNV_PHB3_NUM_REGS     (0x1000 >> 3)
#define PNV_PHB3_NUM_LSI      8
#define PNV_PHB3_NUM_PE       256

#define PCI_MMIO_TOTAL_SIZE   (0x1ull << 60)

struct PnvPHB3 {
    DeviceState parent;

    PnvPHB *phb_base;

    uint32_t chip_id;
    uint32_t phb_id;
    char bus_path[8];

    uint64_t regs[PNV_PHB3_NUM_REGS];
    MemoryRegion mr_regs;

    MemoryRegion mr_m32;
    MemoryRegion mr_m64[PNV_PHB3_NUM_M64];
    MemoryRegion pci_mmio;
    MemoryRegion pci_io;

    uint64_t ioda_LIST[8];
    uint64_t ioda_LXIVT[8];
    uint64_t ioda_TVT[512];
    uint64_t ioda_M64BT[16];
    uint64_t ioda_MDT[256];
    uint64_t ioda_PEEV[4];

    uint32_t total_irq;
    ICSState lsis;
    qemu_irq *qirqs;
    Phb3MsiState msis;

    PnvPBCQState pbcq;

    QLIST_HEAD(, PnvPhb3DMASpace) dma_spaces;

    PnvChip *chip;
};

uint64_t pnv_phb3_reg_read(void *opaque, hwaddr off, unsigned size);
void pnv_phb3_reg_write(void *opaque, hwaddr off, uint64_t val, unsigned size);
void pnv_phb3_update_regions(PnvPHB3 *phb);
void pnv_phb3_remap_irqs(PnvPHB3 *phb);
void pnv_phb3_bus_init(DeviceState *dev, PnvPHB3 *phb);

#endif /* PCI_HOST_PNV_PHB3_H */
