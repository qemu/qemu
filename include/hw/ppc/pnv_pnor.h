/*
 * QEMU PowerNV PNOR simple model
 *
 * Copyright (c) 2019, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_PNV_PNOR_H
#define PPC_PNV_PNOR_H

#include "hw/sysbus.h"

/*
 * PNOR offset on the LPC FW address space
 */
#define PNOR_SPI_OFFSET         0x0c000000UL

#define TYPE_PNV_PNOR  "pnv-pnor"
OBJECT_DECLARE_SIMPLE_TYPE(PnvPnor, PNV_PNOR)

struct PnvPnor {
    SysBusDevice   parent_obj;

    BlockBackend   *blk;

    uint8_t        *storage;
    int64_t        size;
    MemoryRegion   mmio;
};

#endif /* PPC_PNV_PNOR_H */
