/*
 * QEMU PowerPC XIVE interrupt controller model
 *
 * Copyright (c) 2017-2019, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_PNV_XIVE_H
#define PPC_PNV_XIVE_H

#include "hw/ppc/xive.h"
#include "qom/object.h"

struct PnvChip;

#define TYPE_PNV_XIVE "pnv-xive"
OBJECT_DECLARE_TYPE(PnvXive, PnvXiveClass,
                    PNV_XIVE)

#define XIVE_BLOCK_MAX      16

#define XIVE_TABLE_BLK_MAX  16  /* Block Scope Table (0-15) */
#define XIVE_TABLE_MIG_MAX  16  /* Migration Register Table (1-15) */
#define XIVE_TABLE_VDT_MAX  16  /* VDT Domain Table (0-15) */
#define XIVE_TABLE_EDT_MAX  64  /* EDT Domain Table (0-63) */

struct PnvXive {
    XiveRouter    parent_obj;

    /* Owning chip */
    struct PnvChip *chip;

    /* XSCOM addresses giving access to the controller registers */
    MemoryRegion  xscom_regs;

    /* Main MMIO regions that can be configured by FW */
    MemoryRegion  ic_mmio;
    MemoryRegion    ic_reg_mmio;
    MemoryRegion    ic_notify_mmio;
    MemoryRegion    ic_lsi_mmio;
    MemoryRegion    tm_indirect_mmio;
    MemoryRegion  vc_mmio;
    MemoryRegion  pc_mmio;
    MemoryRegion  tm_mmio;

    /*
     * IPI and END address spaces modeling the EDT segmentation in the
     * VC region
     */
    AddressSpace  ipi_as;
    MemoryRegion  ipi_mmio;
    MemoryRegion    ipi_edt_mmio;

    AddressSpace  end_as;
    MemoryRegion  end_mmio;
    MemoryRegion    end_edt_mmio;

    /* Shortcut values for the Main MMIO regions */
    hwaddr        ic_base;
    uint32_t      ic_shift;
    hwaddr        vc_base;
    uint32_t      vc_shift;
    hwaddr        pc_base;
    uint32_t      pc_shift;
    hwaddr        tm_base;
    uint32_t      tm_shift;

    /* Our XIVE source objects for IPIs and ENDs */
    XiveSource    ipi_source;
    XiveENDSource end_source;

    /* Interrupt controller registers */
    uint64_t      regs[0x300];

    /*
     * Virtual Structure Descriptor tables : EAT, SBE, ENDT, NVTT, IRQ
     * These are in a SRAM protected by ECC.
     */
    uint64_t      vsds[5][XIVE_BLOCK_MAX];

    /* Translation tables */
    uint64_t      blk[XIVE_TABLE_BLK_MAX];
    uint64_t      mig[XIVE_TABLE_MIG_MAX];
    uint64_t      vdt[XIVE_TABLE_VDT_MAX];
    uint64_t      edt[XIVE_TABLE_EDT_MAX];
};

struct PnvXiveClass {
    XiveRouterClass parent_class;

    DeviceRealize parent_realize;
};

void pnv_xive_pic_print_info(PnvXive *xive, Monitor *mon);

#endif /* PPC_PNV_XIVE_H */
