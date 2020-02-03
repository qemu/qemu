/*
 * QEMU PowerPC PowerNV (POWER9) PHB4 model
 *
 * Copyright (c) 2018-2020, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PCI_HOST_PNV_PHB4_H
#define PCI_HOST_PNV_PHB4_H

#include "hw/pci/pcie_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/ppc/xive.h"

typedef struct PnvPhb4PecState PnvPhb4PecState;
typedef struct PnvPhb4PecStack PnvPhb4PecStack;
typedef struct PnvPHB4 PnvPHB4;
typedef struct PnvChip PnvChip;

/*
 * We have one such address space wrapper per possible device under
 * the PHB since they need to be assigned statically at qemu device
 * creation time. The relationship to a PE is done later
 * dynamically. This means we can potentially create a lot of these
 * guys. Q35 stores them as some kind of radix tree but we never
 * really need to do fast lookups so instead we simply keep a QLIST of
 * them for now, we can add the radix if needed later on.
 *
 * We do cache the PE number to speed things up a bit though.
 */
typedef struct PnvPhb4DMASpace {
    PCIBus *bus;
    uint8_t devfn;
    int pe_num;         /* Cached PE number */
#define PHB_INVALID_PE (-1)
    PnvPHB4 *phb;
    AddressSpace dma_as;
    IOMMUMemoryRegion dma_mr;
    MemoryRegion msi32_mr;
    MemoryRegion msi64_mr;
    QLIST_ENTRY(PnvPhb4DMASpace) list;
} PnvPhb4DMASpace;

/*
 * PHB4 PCIe Root port
 */
#define TYPE_PNV_PHB4_ROOT_BUS "pnv-phb4-root-bus"
#define TYPE_PNV_PHB4_ROOT_PORT "pnv-phb4-root-port"

typedef struct PnvPHB4RootPort {
    PCIESlot parent_obj;
} PnvPHB4RootPort;

/*
 * PHB4 PCIe Host Bridge for PowerNV machines (POWER9)
 */
#define TYPE_PNV_PHB4 "pnv-phb4"
#define PNV_PHB4(obj) OBJECT_CHECK(PnvPHB4, (obj), TYPE_PNV_PHB4)

#define PNV_PHB4_MAX_LSIs          8
#define PNV_PHB4_MAX_INTs          4096
#define PNV_PHB4_MAX_MIST          (PNV_PHB4_MAX_INTs >> 2)
#define PNV_PHB4_MAX_MMIO_WINDOWS  32
#define PNV_PHB4_MIN_MMIO_WINDOWS  16
#define PNV_PHB4_NUM_REGS          (0x3000 >> 3)
#define PNV_PHB4_MAX_PEs           512
#define PNV_PHB4_MAX_TVEs          (PNV_PHB4_MAX_PEs * 2)
#define PNV_PHB4_MAX_PEEVs         (PNV_PHB4_MAX_PEs / 64)
#define PNV_PHB4_MAX_MBEs          (PNV_PHB4_MAX_MMIO_WINDOWS * 2)

#define PNV_PHB4_VERSION           0x000000a400000002ull
#define PNV_PHB4_DEVICE_ID         0x04c1

#define PCI_MMIO_TOTAL_SIZE        (0x1ull << 60)

struct PnvPHB4 {
    PCIExpressHost parent_obj;

    PnvPHB4RootPort root;

    uint32_t chip_id;
    uint32_t phb_id;

    uint64_t version;
    uint16_t device_id;

    char bus_path[8];

    /* Main register images */
    uint64_t regs[PNV_PHB4_NUM_REGS];
    MemoryRegion mr_regs;

    /* Extra SCOM-only register */
    uint64_t scom_hv_ind_addr_reg;

    /*
     * Geometry of the PHB. There are two types, small and big PHBs, a
     * number of resources (number of PEs, windows etc...) are doubled
     * for a big PHB
     */
    bool big_phb;

    /* Memory regions for MMIO space */
    MemoryRegion mr_mmio[PNV_PHB4_MAX_MMIO_WINDOWS];

    /* PCI side space */
    MemoryRegion pci_mmio;
    MemoryRegion pci_io;

    /* On-chip IODA tables */
    uint64_t ioda_LIST[PNV_PHB4_MAX_LSIs];
    uint64_t ioda_MIST[PNV_PHB4_MAX_MIST];
    uint64_t ioda_TVT[PNV_PHB4_MAX_TVEs];
    uint64_t ioda_MBT[PNV_PHB4_MAX_MBEs];
    uint64_t ioda_MDT[PNV_PHB4_MAX_PEs];
    uint64_t ioda_PEEV[PNV_PHB4_MAX_PEEVs];

    /*
     * The internal PESTA/B is 2 bits per PE split into two tables, we
     * store them in a single array here to avoid wasting space.
     */
    uint8_t  ioda_PEST_AB[PNV_PHB4_MAX_PEs];

    /* P9 Interrupt generation */
    XiveSource xsrc;
    qemu_irq *qirqs;

    PnvPhb4PecStack *stack;

    QLIST_HEAD(, PnvPhb4DMASpace) dma_spaces;
};

void pnv_phb4_pic_print_info(PnvPHB4 *phb, Monitor *mon);
void pnv_phb4_update_regions(PnvPhb4PecStack *stack);
extern const MemoryRegionOps pnv_phb4_xscom_ops;

/*
 * PHB4 PEC (PCI Express Controller)
 */
#define TYPE_PNV_PHB4_PEC "pnv-phb4-pec"
#define PNV_PHB4_PEC(obj) \
    OBJECT_CHECK(PnvPhb4PecState, (obj), TYPE_PNV_PHB4_PEC)

#define TYPE_PNV_PHB4_PEC_STACK "pnv-phb4-pec-stack"
#define PNV_PHB4_PEC_STACK(obj) \
    OBJECT_CHECK(PnvPhb4PecStack, (obj), TYPE_PNV_PHB4_PEC_STACK)

/* Per-stack data */
struct PnvPhb4PecStack {
    DeviceState parent;

    /* My own stack number */
    uint32_t stack_no;

    /* Nest registers */
#define PHB4_PEC_NEST_STK_REGS_COUNT  0x17
    uint64_t nest_regs[PHB4_PEC_NEST_STK_REGS_COUNT];
    MemoryRegion nest_regs_mr;

    /* PCI registers (excluding pass-through) */
#define PHB4_PEC_PCI_STK_REGS_COUNT  0xf
    uint64_t pci_regs[PHB4_PEC_PCI_STK_REGS_COUNT];
    MemoryRegion pci_regs_mr;

    /* PHB pass-through XSCOM */
    MemoryRegion phb_regs_mr;

    /* Memory windows from PowerBus to PHB */
    MemoryRegion mmbar0;
    MemoryRegion mmbar1;
    MemoryRegion phbbar;
    MemoryRegion intbar;
    uint64_t mmio0_base;
    uint64_t mmio0_size;
    uint64_t mmio1_base;
    uint64_t mmio1_size;

    /* The owner PEC */
    PnvPhb4PecState *pec;

    /* The actual PHB */
    PnvPHB4 phb;
};

struct PnvPhb4PecState {
    DeviceState parent;

    /* PEC number in chip */
    uint32_t index;
    uint32_t chip_id;

    MemoryRegion *system_memory;

    /* Nest registers, excuding per-stack */
#define PHB4_PEC_NEST_REGS_COUNT    0xf
    uint64_t nest_regs[PHB4_PEC_NEST_REGS_COUNT];
    MemoryRegion nest_regs_mr;

    /* PCI registers, excluding per-stack */
#define PHB4_PEC_PCI_REGS_COUNT     0x2
    uint64_t pci_regs[PHB4_PEC_PCI_REGS_COUNT];
    MemoryRegion pci_regs_mr;

    /* Stacks */
    #define PHB4_PEC_MAX_STACKS     3
    uint32_t num_stacks;
    PnvPhb4PecStack stacks[PHB4_PEC_MAX_STACKS];
};

#define PNV_PHB4_PEC_CLASS(klass) \
     OBJECT_CLASS_CHECK(PnvPhb4PecClass, (klass), TYPE_PNV_PHB4_PEC)
#define PNV_PHB4_PEC_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PnvPhb4PecClass, (obj), TYPE_PNV_PHB4_PEC)

typedef struct PnvPhb4PecClass {
    DeviceClass parent_class;

    uint32_t (*xscom_nest_base)(PnvPhb4PecState *pec);
    uint32_t xscom_nest_size;
    uint32_t (*xscom_pci_base)(PnvPhb4PecState *pec);
    uint32_t xscom_pci_size;
    const char *compat;
    int compat_size;
    const char *stk_compat;
    int stk_compat_size;
} PnvPhb4PecClass;

#endif /* PCI_HOST_PNV_PHB4_H */
