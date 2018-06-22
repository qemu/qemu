/*
 * ARM AHB5 TrustZone Memory Protection Controller emulation
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

/* This is a model of the TrustZone memory protection controller (MPC).
 * It is documented in the ARM CoreLink SIE-200 System IP for Embedded TRM
 * (DDI 0571G):
 * https://developer.arm.com/products/architecture/m-profile/docs/ddi0571/g
 *
 * The MPC sits in front of memory and allows secure software to
 * configure it to either pass through or reject transactions.
 * Rejected transactions may be configured to either be aborted, or to
 * behave as RAZ/WI. An interrupt can be signalled for a rejected transaction.
 *
 * The MPC has a register interface which the guest uses to configure it.
 *
 * QEMU interface:
 * + sysbus MMIO region 0: MemoryRegion for the MPC's config registers
 * + sysbus MMIO region 1: MemoryRegion for the upstream end of the MPC
 * + Property "downstream": MemoryRegion defining the downstream memory
 * + Named GPIO output "irq": set for a transaction-failed interrupt
 */

#ifndef TZ_MPC_H
#define TZ_MPC_H

#include "hw/sysbus.h"

#define TYPE_TZ_MPC "tz-mpc"
#define TZ_MPC(obj) OBJECT_CHECK(TZMPC, (obj), TYPE_TZ_MPC)

#define TZ_NUM_PORTS 16

#define TYPE_TZ_MPC_IOMMU_MEMORY_REGION "tz-mpc-iommu-memory-region"

typedef struct TZMPC TZMPC;

struct TZMPC {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/

    /* State */
    uint32_t ctrl;
    uint32_t blk_idx;
    uint32_t int_stat;
    uint32_t int_en;
    uint32_t int_info1;
    uint32_t int_info2;

    uint32_t *blk_lut;

    qemu_irq irq;

    /* Properties */
    MemoryRegion *downstream;

    hwaddr blocksize;
    uint32_t blk_max;

    /* MemoryRegions exposed to user */
    MemoryRegion regmr;
    IOMMUMemoryRegion upstream;

    /* MemoryRegion used internally */
    MemoryRegion blocked_io;

    AddressSpace downstream_as;
    AddressSpace blocked_io_as;
};

#endif
